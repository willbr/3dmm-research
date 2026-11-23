/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Shared (platform independent) file APIs.

***************************************************************************/
#include <system_error>
#include "util.h"
#include "handle_services.h"
#include "mm/file_text.h"
ASSERTNAME

namespace {
int ReadBlockFile(void *file, MMU32 offset, void *bytes, size_t count)
{
    return !count || static_cast<PFileObject>(file)->FReadRgb(bytes, count, offset);
}
int WriteBlockFile(void *file, MMU32 offset, const void *bytes, size_t count)
{
    return !count || static_cast<PFileObject>(file)->FWriteRgb(const_cast<void *>(bytes), count, offset);
}
MMBlockIO FileBlock(PFileLocation location)
{
    MMBlockIO io = {};
    io.view.kind = MM_BLOCK_FILE; io.view.start = location->fp; io.view.length = location->cb;
    io.handles = KauaiHandleServices(); io.context = location->pfil;
    io.read_file = ReadBlockFile; io.write_file = WriteBlockFile;
    return io;
}
void *AllocateCopyBuffer(void *, void *old, MMU32 oldBytes, MMU32 bytes, int priority)
{
    void *buffer = old;
    const LegacyLong mpr = priority == MM_STORAGE_SPEED ? mprForSpeed : mprNormal;
    const bool ok = old ? _FResizePpvDebug(&buffer, bytes, oldBytes, fmemNil, mpr, &vdmglob.dmaglPv) :
                          FAllocPv(&buffer, bytes, fmemNil, mpr);
    return ok ? buffer : nullptr;
}
void ReleaseCopyBuffer(void *, void *buffer, MMU32) { FreePpv(&buffer); }
const MMStorageAllocator copyAllocator = {nullptr, AllocateCopyBuffer, ReleaseCopyBuffer};
void *AllocateResourceObject(void *, void *, MMU32, MMU32 bytes, int)
{
    void *memory = nullptr;
    if (!FAllocPvDebug(&memory, bytes, fmemNil, mprNormal, __szsFile, __LINE__, &vdmglob.dmaglBase))
        PushErc(ercOomNew);
    return memory;
}
void ReleaseResourceObject(void *, void *memory, MMU32) { FreePpvDebug(&memory, &vdmglob.dmaglBase); }
const MMStorageAllocator resourceObjects = {nullptr, AllocateResourceObject, ReleaseResourceObject};
void RetainBlockFile(void *, void *file) { static_cast<PFileObject>(file)->AddRef(); }
void ReleaseBlockFile(void *, void *file) { static_cast<PFileObject>(file)->Release(); }
void *CreateBlockTemp(void *) { return FileObject::PfilCreateTemp(); }
int ReadOwnedFile(void *, void *file, MMU32 offset, void *bytes, size_t count) { return ReadBlockFile(file, offset, bytes, count); }
int WriteOwnedFile(void *, void *file, MMU32 offset, const void *bytes, size_t count) { return WriteBlockFile(file, offset, bytes, count); }
const MMBlockFileServices blockFiles = {nullptr, RetainBlockFile, ReleaseBlockFile, CreateBlockTemp, ReadOwnedFile, WriteOwnedFile};
int PackHandle(void *context, void **handle) { return vpcodmUtil->FCompressPhq(handle, *static_cast<LegacyLong *>(context)); }
MMI32 ConvertFileText(void *, const void *input, MMU32 bytes, int source, void *output, MMU32 capacity)
{
    return CchTranslateRgb(const_cast<void *>(input), bytes, static_cast<short>(source), static_cast<achar *>(output), capacity);
}
}
const MMStorageAllocator *KauaiCopyAllocator() { return &copyAllocator; }
const MMStorageAllocator *KauaiResourceObjectAllocator() { return &resourceObjects; }
const MMBlockFileServices *KauaiBlockFileServices() { return &blockFiles; }
int KauaiDecodeBlockHandle(void *, void **handle) { return vpcodmUtil->FDecompressPhq(handle); }

FileType FileObject::vftgCreator = '____';

RTCLASS(FileObject)
RTCLASS(DataBlock)
RTCLASS(MessageSinkFile)

MMFileRegistry *FileObject::_Registry()
{
    static MMFileRegistry registry{};
    // File lookups and final releases can run from other global destructors.
    static const bool initialized = [] {
        int error = 0;
        if (!mm_native_file_registry_init(&registry, &error))
            throw std::system_error(error, std::system_category());
        return true;
    }();
    (void)initialized;
    return &registry;
}

namespace {
PFileObject FileFromRegistration(MMRegisteredFile *record)
{
    return record ? static_cast<PFileObject>(record->object) : nullptr;
}
}

PFileObject FileObject::PfilFirst(void)
{
    return FileFromRegistration(mm_file_registry_first(_Registry()));
}

PFileObject FileObject::PfilNext(void)
{
    return FileFromRegistration(mm_file_registry_next(&_registration));
}

MMFileOpsHost FileObject::CoreFileOps()
{
    static_assert(tNo == MM_FILE_NAME_MISSING, "native file status contract");
    MMFileOpsHost host{};
    host.registry = _Registry();
    host.equal_names = [](void *, void *a, void *b) -> int { return static_cast<Filename *>(a)->FEqual(static_cast<Filename *>(b)); };
    host.copy_name = [](void *, void *dst, const void *src) { *static_cast<Filename *>(dst) = *static_cast<Filename *>(const_cast<void *>(src)); };
    host.unique_name = [](void *, void *p) -> int { auto *name = static_cast<Filename *>(p); return name->FGetUnique(name->Ftg()); };
    host.temp_name = [](void *, void *p) -> int { return static_cast<Filename *>(p)->FGetTemp(); };
    host.same_directory = [](void *, void *a, void *b) -> int { return static_cast<Filename *>(a)->FSameDir(static_cast<Filename *>(b)); };
    host.rename_name = [](void *, void *a, void *b) -> int { return static_cast<Filename *>(a)->FRename(static_cast<Filename *>(b)); };
    host.name_status = [](void *, void *p) -> int { return static_cast<Filename *>(p)->TExists(); };
    host.delete_name = [](void *, void *p) -> int { return static_cast<Filename *>(p)->FDelete(); };
    host.construct = [](void *, void *p, MMU32 flags) -> MMRegisteredFile * {
        auto *file = NewObj FileObject(static_cast<Filename *>(p), flags);
        return file ? &file->_registration : nullptr;
    };
    host.retain = [](void *, void *p) { static_cast<PFileObject>(p)->AddRef(); };
    host.destroy = [](void *, void *p) { delete static_cast<PFileObject>(p); };
    host.issue = [](void *, int code) {
        switch (code) {
        case MM_FILE_OP_OPEN: PushErc(ercFileOpen); break;
        case MM_FILE_OP_CREATE: PushErc(ercFileCreate); break;
        case MM_FILE_OP_SWAP: PushErc(ercFileSwapNames); break;
        case MM_FILE_OP_RENAME: PushErc(ercFileRename); break;
        }
    };
#ifdef DEBUG
    host.validate_name = [](void *, void *p) { AssertPo(static_cast<Filename *>(p), ffniFile); };
    host.validate_object = [](void *, void *p) { AssertPo(static_cast<PFileObject>(p), 0); };
    host.assertion = [](void *, int code) {
        switch (code) {
        case MM_FILE_OP_TEMP_OPEN: Assert(fFalse, "can't open a file as temp"); break;
        case MM_FILE_OP_CREATE_OPEN: Bug("trying to create an open file"); break;
        case MM_FILE_OP_SWAP_SELF: Bug("Why are you calling FSwapNames on the same file?"); break;
        case MM_FILE_OP_SWAP_DIRECTORY: Assert(fFalse, "trying to change directories with FSwapNames"); break;
        case MM_FILE_OP_RENAME_DIRECTORY: Assert(fFalse, "trying to change directories with FRename"); break;
        case MM_FILE_OP_RECOVERY: Bug("rename failure"); break;
        }
    };
#endif
    return host;
}

MMRegisteredFile *FileObject::CoreFileRegistration(void *file)
{
    return &static_cast<PFileObject>(file)->_registration;
}

/***************************************************************************
    Constructor for a file.
***************************************************************************/
FileObject::FileObject(Filename *pfni, LegacyUlong grffil)
{
    AssertPo(pfni, ffniFile);
    auto *registry = _Registry();
    _fni = *pfni;
    int error = 0;
    if (!mm_native_named_file_init(&_file, reinterpret_cast<const MMNativePathChar *>(_fni._Path()),
                                   grffil, _FileDiagnostics(), this, &error))
        throw std::system_error(error, std::system_category());

    mm_native_named_file_register(&_file, registry, &_registration, this, &_fni, &_references);
}

/***************************************************************************
    Destructor.  This is private.
***************************************************************************/
FileObject::~FileObject(void)
{
    _Close(fTrue);

    mm_file_registry_detach(_Registry(), &_registration);

    mm_native_named_file_dispose(&_file);
}

/***************************************************************************
    Static method to open an existing file.  Increments the open count.
***************************************************************************/
PFileObject FileObject::PfilOpen(Filename *pfni, LegacyUlong grffil)
{
    const auto host = CoreFileOps();
    return FileFromRegistration(mm_file_ops_open(&host, pfni, grffil));
}

MMReadStream FileObject::ReadStream()
{
    return mm_native_named_file_stream(&_file);
}

/***************************************************************************
    Create a new file.  Increments the open count.
***************************************************************************/
PFileObject FileObject::PfilCreate(Filename *pfni, LegacyUlong grffil)
{
    const auto host = CoreFileOps();
    return FileFromRegistration(mm_file_ops_create(&host, pfni, grffil));
}

/***************************************************************************
    Static method to create a temp file in the same directory as fni with
    the same ftg, or, if pfni is nil, in the standard place with vftgTemp.
    The file is not marked.
***************************************************************************/
PFileObject FileObject::PfilCreateTemp(Filename *pfni)
{
    Filename scratch;
    const auto host = CoreFileOps();
    return FileFromRegistration(mm_file_ops_temp(&host, pfni, &scratch));
}

/***************************************************************************
    If we have the file indicated by fni open, returns the pfil, otherwise
    returns pvNil.  Doesn't affect the open count.
***************************************************************************/
PFileObject FileObject::PfilFromFni(Filename *pfni)
{
    const auto host = CoreFileOps();
    return FileFromRegistration(mm_file_ops_find(&host, pfni));
}

/***************************************************************************
    Set the file flags according to grffil and grffilMask.  Write enabling
    is only set, never cleared.  Same with marking.
***************************************************************************/
bool FileObject::FSetGrffil(LegacyUlong grffil, LegacyUlong grffilMask)
{
    const auto host = _OwnerHost();
    return mm_file_owner_set_flags(&_file.state, &host, grffil, grffilMask) != 0;
}

/***************************************************************************
    Decrement the open count.  If it is zero and the file isn't marked,
    the file is closed.
***************************************************************************/
void FileObject::Release(void)
{
    AssertThis(0);
    mm_reference_release_marked(&_references, (_file.state.flags & ffilMark) != 0, this,
        [](void *) { Bug("calling Release without an AddRef"); },
        [](void *p) { delete static_cast<PFileObject>(p); });
}

/***************************************************************************
    Get a string representing the path of the file.
***************************************************************************/
void FileObject::GetStnPath(PString pstn)
{
    AssertThis(0);

    mm_native_named_file_enter(&_file);
    _fni.GetStnPath(pstn);
    mm_native_named_file_leave(&_file);
}

/***************************************************************************
    Set the temporary status of a file.
***************************************************************************/
void FileObject::SetTemp(bool fTemp)
{
    const auto host = _OwnerHost();
    mm_file_owner_set_temp(&_file.state, &host, fTemp);
}

/***************************************************************************
    Rename the file to the given file name.  If an open file exists with
    the same name, we rename it and swap names with it as a temporary file.
    Otherwise, we delete any existing file with the same name.  The rules
    for *pfni are the same as for FRename.
***************************************************************************/
bool FileObject::FSetFni(Filename *pfni)
{
    AssertPo(pfni, ffniFile);
    Filename scratch;
    const auto host = CoreFileOps();
    return mm_file_ops_set_name(&host, &_registration, pfni, &scratch) != 0;
}

/***************************************************************************
    Static method to clear the marks for files.
***************************************************************************/
void FileObject::ClearMarks(void)
{
    const auto host = CoreFileOps();
    mm_file_ops_clear_marks(&host);
}

/***************************************************************************
    Static method to close any files that are unmarked and have 0 open count.
***************************************************************************/
void FileObject::CloseUnmarked(void)
{
    const auto host = CoreFileOps();
    mm_file_ops_close_unmarked(&host);
}

/***************************************************************************
    Static method to close all files.
***************************************************************************/
void FileObject::ShutDown(void)
{
    const auto host = CoreFileOps();
    mm_file_ops_shutdown(&host);
}

#ifdef DEBUG
/***************************************************************************
    Validate a pfil.
***************************************************************************/
void FileObject::AssertValid(LegacyUlong grf)
{
    FileObject_PAR::AssertValid(fobjAllocated);
    mm_native_named_file_enter(&_file);
    AssertPo(&_fni, ffniFile);
    mm_native_named_file_leave(&_file);
    const bool found = mm_file_registry_contains(_Registry(), &_registration) != 0;
    Assert(found, "not in file list");
}
#endif // DEBUG

/***************************************************************************
    Determine if the given range is within cbTot.
***************************************************************************/
priv bool _FRangeIn(LegacyLong cbTot, LegacyLong cb, LegacyLong ib)
{
    return cbTot >= 0 && cb >= 0 && ib >= 0 && mm_range_contains(cbTot, ib, cb);
}

/***************************************************************************
    Read a piece of a flo into pv.
***************************************************************************/
bool FileLocation::FReadRgb(void *pv, LegacyLong cbRead, FilePosition dfp)
{
    AssertThis(ffloReadable);

    if (!_FRangeIn(this->cb, cbRead, dfp))
    {
        Bug("reading outside flo");
        return fFalse;
    }

    if (cbRead == 0)
        return fTrue;
    return this->pfil->FReadRgb(pv, cbRead, this->fp + dfp);
}

/***************************************************************************
    Write a piece of a flo from pv.
***************************************************************************/
bool FileLocation::FWriteRgb(void *pv, LegacyLong cbWrite, FilePosition dfp)
{
    AssertThis(0);

    if (!_FRangeIn(this->cb, cbWrite, dfp))
    {
        Bug("writing outside flo");
        return fFalse;
    }

    if (cbWrite == 0)
        return fTrue;
    return this->pfil->FWriteRgb(pv, cbWrite, this->fp + dfp);
}

/***************************************************************************
    Copy data from this flo to another.
***************************************************************************/
bool FileLocation::FCopy(PFileLocation pfloDst)
{
    AssertThis(ffloReadable);
    AssertPo(pfloDst, 0);
    if (cb != pfloDst->cb) { Bug("different sized FLOs"); return false; }
    const auto source = FileBlock(this), destination = FileBlock(pfloDst);
    return mm_block_copy(&source, &destination, true, &copyAllocator) == MM_DATA_OK;
}

/***************************************************************************
    Allocate an hq and read the flo into it.
***************************************************************************/
bool FileLocation::FReadHq(HQ *phq, LegacyLong cbRead, FilePosition dfp)
{
    AssertThis(ffloReadable);
    AssertVarMem(phq);
    if (!_FRangeIn(cb, cbRead, dfp)) { Bug("reading outside flo 2"); return false; }
    const auto io = FileBlock(this);
    return mm_block_read_handle(&io, dfp, cbRead, true, phq) == MM_DATA_OK;
}

/***************************************************************************
    Write the contents of an hq to the flo.
***************************************************************************/
bool FileLocation::FWriteHq(HQ hq, LegacyLong dfp)
{
    AssertThis(0);
    AssertHq(hq);
    const auto io = FileBlock(this);
    const int result = mm_block_write_handle(&io, dfp, hq, true);
    if (result == MM_DATA_ARGUMENT) Bug("writing outside flo 2");
    return result == MM_DATA_OK;
}

/***************************************************************************
    Translate the text in a flo from the given osk to the current osk.
    If the text changes, creates a temp file and redirects the flo to the
    temp file (and releases a ref count on the pfil).
***************************************************************************/
bool FileLocation::FTranslate(short osk)
{
    AssertThis(0);
    const short one = 1;
    const MMFileTextConverter converter = {nullptr, koskCur, MacWin(koskSbMac, koskSbWin),
        MacWin(koskUniMac, koskUniWin), reinterpret_cast<const byte *>(&one)[0] == 0, sizeof(achar), ConvertFileText};
    MMBlockFile region = {pfil, static_cast<MMU32>(fp), static_cast<MMU32>(cb)};
    const int result = mm_file_text_translate(&region, &blockFiles, &converter, KauaiCopyAllocator(), osk);
    // BOM stripping remains visible even if a later allocation or I/O fails.
    pfil = static_cast<PFileObject>(region.file); fp = region.offset; cb = region.length;
    if (result == MM_DATA_WRONG_SIZE) Bug("why did CchTranslateRgb fail?");
    return result == MM_DATA_OK;
}

#ifdef DEBUG
/***************************************************************************
    Assert this is a valif FileLocation.
***************************************************************************/
void FileLocation::AssertValid(LegacyUlong grfflo)
{
    AssertPo(pfil, 0);
    AssertIn(fp, 0, kcbMax);
    AssertIn(cb, 0, kcbMax);
    FilePosition fpMac = pfil->FpMac();

    if (pfil->ElError() < kelSeek)
    {
        AssertIn(fp, 0, fpMac + 1);
        if (grfflo & ffloReadable)
            AssertIn(fp + cb, cb, fpMac + 1);
    }
}
#endif // DEBUG

/***************************************************************************
    Constructor for a data block.
***************************************************************************/
MMBlockIO DataBlock::_IOView()
{
    MMBlockIO io = {};
    AssertDo(mm_owned_block_io(&_state, &io) == MM_DATA_OK, "invalid block state");
    return io;
}
DataBlock::DataBlock(PFileLocation pflo, bool fPacked)
{
    AssertBaseThis(0);
    AssertDo(mm_owned_block_init(&_state, KauaiHandleServices(), &blockFiles) == MM_DATA_OK, "invalid block services");
    Set(pflo, fPacked);
}

/***************************************************************************
    Constructor for a data block.
***************************************************************************/
DataBlock::DataBlock(PFileObject pfil, FilePosition fp, LegacyLong cb, bool fPacked)
{
    AssertBaseThis(0);
    AssertDo(mm_owned_block_init(&_state, KauaiHandleServices(), &blockFiles) == MM_DATA_OK, "invalid block services");
    Set(pfil, fp, cb, fPacked);
}

/***************************************************************************
    Another constructor for a data block.  Assumes ownership of the hq
    (and sets *phq to hqNil).
***************************************************************************/
DataBlock::DataBlock(HQ *phq, bool fPacked)
{
    AssertBaseThis(0);
    AssertDo(mm_owned_block_init(&_state, KauaiHandleServices(), &blockFiles) == MM_DATA_OK, "invalid block services");
    SetHq(phq, fPacked);
}

/***************************************************************************
    Another constructor for a data block.
***************************************************************************/
DataBlock::DataBlock(void)
{
    AssertBaseThis(0);
    AssertDo(mm_owned_block_init(&_state, KauaiHandleServices(), &blockFiles) == MM_DATA_OK, "invalid block services");
    AssertThis(0);
}

/***************************************************************************
    The destructor.
***************************************************************************/
DataBlock::~DataBlock(void)
{
    AssertThis(0);
    Free();
}

/***************************************************************************
    Set the data block to refer to the given flo.
***************************************************************************/
void DataBlock::Set(PFileLocation pflo, bool fPacked)
{
    AssertThis(0);
    AssertPo(pflo, 0);
    Set(pflo->pfil, pflo->fp, pflo->cb, fPacked);
}

/***************************************************************************
    Set the data block to refer to the given range on the file.
***************************************************************************/
void DataBlock::Set(PFileObject pfil, FilePosition fp, LegacyLong cb, bool fPacked)
{
    AssertThis(0);
    AssertPo(pfil, 0);
    const MMBlockFile file = {pfil, static_cast<MMU32>(fp), static_cast<MMU32>(cb)};
    AssertDo(mm_owned_block_set_file(&_state, &file, fPacked) == MM_DATA_OK, "invalid file block");
    AssertThis(0);
}

/***************************************************************************
    Set the data block to the given hq.  Assumes ownership of the hq and
    sets *phq to hqNil.
***************************************************************************/
void DataBlock::SetHq(HQ *phq, bool fPacked)
{
    AssertThis(0);
    AssertVarMem(phq);
    AssertHq(*phq);
    AssertDo(mm_owned_block_set_handle(&_state, phq, fPacked) == MM_DATA_OK, "invalid memory block");
    AssertThis(0);
}

/***************************************************************************
    Free the block (make it empty).
***************************************************************************/
void DataBlock::Free(void)
{
    AssertThis(0);
    AssertDo(mm_owned_block_clear(&_state) == MM_DATA_OK, "invalid block release");
    AssertThis(0);
}

/***************************************************************************
    Return an hq to the data.  If the blck is a memory based block, the
    block is also "freed".  If the block hasn't been packed or unpacked
    or had its min or lim moved, the hq returned is the one originally
    passed to the constructor or SetHq.
***************************************************************************/
HQ DataBlock::HqFree(bool fPackedOk)
{
    AssertThis(0);
    HQ handle = nullptr;
    int shrinkFailed = 0;
    const int result = mm_owned_block_take_handle(&_state, fPackedOk, &handle, &shrinkFailed);
    if (result == MM_DATA_PACKED) Bug("accessing packed data");
    Assert(!shrinkFailed, "HQ shrink failed");
    AssertThis(0);
    return result == MM_DATA_OK ? handle : nullptr;
}

/***************************************************************************
    Return the length of the data block.
***************************************************************************/
LegacyLong DataBlock::Cb(bool fPackedOk)
{
    AssertThis(fPackedOk ? 0 : fblckUnpacked);
    return _state.view.length;
}

/***************************************************************************
    Create a temporary buffer.
***************************************************************************/
bool DataBlock::FSetTemp(LegacyLong cb, bool fForceFile)
{
    AssertThis(0);
    const bool ok = mm_owned_block_set_temp(&_state, cb, fForceFile) == MM_DATA_OK;
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Move the beginning of the block.  Doesn't change the location of the
    end of the block.  Fails if you try to move before the beginning of
    the physical storage or after the lim of the block.
***************************************************************************/
bool DataBlock::FMoveMin(LegacyLong dib)
{
    AssertThis(0);
    return mm_block_move_min(&_state.view, dib) == MM_DATA_OK;
}

/***************************************************************************
    Move the end of the block.  Doesn't change the location of the
    beginning of the block.  Fails if you try to move before the min of the
    block or after the end of the physical storage.
***************************************************************************/
bool DataBlock::FMoveLim(LegacyLong dib)
{
    AssertThis(0);
    return mm_block_move_lim(&_state.view, dib) == MM_DATA_OK;
}

/***************************************************************************
    Read a range of bytes from the data block.
***************************************************************************/
bool DataBlock::FReadRgb(void *pv, LegacyLong cb, LegacyLong ib, bool fPackedOk)
{
    AssertThis(0);
    AssertPvCb(pv, cb);
    const auto io = _IOView();
    const int result = mm_block_read(&io, ib, pv, cb, fPackedOk);
    if (result == MM_DATA_PACKED || result == MM_DATA_ARGUMENT)
        Bug((result == MM_DATA_PACKED ? "reading packed data" : "reading outside blck"));
    return result == MM_DATA_OK;
}

/***************************************************************************
    Write a range of bytes to the data block.
***************************************************************************/
bool DataBlock::FWriteRgb(void *pv, LegacyLong cb, LegacyLong ib, bool fPackedOk)
{
    AssertThis(0);
    AssertPvCb(pv, cb);
    const auto io = _IOView();
    const int result = mm_block_write(&io, ib, pv, cb, fPackedOk);
    if (result == MM_DATA_PACKED || result == MM_DATA_ARGUMENT)
        Bug((result == MM_DATA_PACKED ? "writing packed data" : "writing outside blck"));
    return result == MM_DATA_OK;
}

/***************************************************************************
    Read a range of bytes from the data block and put it in an hq.
***************************************************************************/
bool DataBlock::FReadHq(HQ *phq, LegacyLong cb, LegacyLong ib, bool fPackedOk)
{
    AssertThis(0);
    AssertVarMem(phq);
    const auto io = _IOView();
    const int result = mm_block_read_handle(&io, ib, cb, fPackedOk, phq);
    if (result == MM_DATA_PACKED || result == MM_DATA_ARGUMENT)
        Bug((result == MM_DATA_PACKED ? "reading packed data" : "reading outside blck"));
    return result == MM_DATA_OK;
}

/***************************************************************************
    Write an hq to the data block.
***************************************************************************/
bool DataBlock::FWriteHq(HQ hq, LegacyLong ib, bool fPackedOk)
{
    AssertThis(0);
    AssertHq(hq);
    const auto io = _IOView();
    const int result = mm_block_write_handle(&io, ib, hq, fPackedOk);
    if (result == MM_DATA_PACKED || result == MM_DATA_ARGUMENT)
        Bug((result == MM_DATA_PACKED ? "writing packed data" : "writing outside blck"));
    return result == MM_DATA_OK;
}

/***************************************************************************
    Write the block to a flo.
***************************************************************************/
bool DataBlock::FWriteToFlo(PFileLocation pfloDst, bool fPackedOk)
{
    AssertThis(fblckReadable);
    AssertPo(pfloDst, 0);
    const auto source = _IOView(), destination = FileBlock(pfloDst);
    const int result = mm_block_copy(&source, &destination, fPackedOk, &copyAllocator);
    if (result == MM_DATA_PACKED) Bug("copying packed data");
    if (result == MM_DATA_WRONG_SIZE) Bug("flo is wrong size");
    return result == MM_DATA_OK;
}

/***************************************************************************
    Write this block to another block.
***************************************************************************/
bool DataBlock::FWriteToBlck(PDataBlock pblckDst, bool fPackedOk)
{
    AssertThis(fblckReadable);
    AssertPo(pblckDst, 0);
    const auto source = _IOView(), destination = pblckDst->_IOView();
    const int result = mm_block_copy(&source, &destination, fPackedOk, &copyAllocator);
    if (result == MM_DATA_PACKED) Bug("copying packed data");
    if (result == MM_DATA_WRONG_SIZE) Bug("blck is wrong size");
    return result == MM_DATA_OK;
}

/***************************************************************************
    Get a flo to the data in the block.
***************************************************************************/
bool DataBlock::FGetFlo(PFileLocation pflo, bool fPackedOk)
{
    AssertThis(0);
    AssertVarMem(pflo);
    MMBlockFile file;
    const int result = mm_owned_block_materialize(&_state, fPackedOk, &file);
    if (result == MM_DATA_PACKED) { Bug("accessing packed data"); return false; }
    if (result != MM_DATA_OK) { TrashVar(pflo); return false; }
    pflo->pfil = static_cast<PFileObject>(file.file); pflo->fp = file.offset; pflo->cb = file.length;
    return true;
}

/***************************************************************************
    Return whether the block is packed. If the block is compressed, but
    determining the compression type failed, *pcfmt is set to cfmtNil and
    true is returned.
***************************************************************************/
bool DataBlock::FPacked(LegacyLong *pcfmt)
{
    AssertThis(0);
    AssertNilOrVarMem(pcfmt);
    if (pcfmt != pvNil && (!_state.view.packed || !vpcodmUtil->FGetCfmtFromBlck(this, pcfmt))) *pcfmt = cfmtNil;
    return _state.view.packed != 0;
}

/***************************************************************************
    If the block is unpacked, pack it. If cfmt is cfmtNil, use the default
    packing format, otherwise use the one specified. If the block is
    already packed, this doesn't change the packing format.
***************************************************************************/
bool DataBlock::FPackData(LegacyLong cfmt)
{
    AssertThis(0);
    int shrinkFailed = 0;
    const int result = mm_owned_block_code(&_state, 1, &cfmt, PackHandle, &shrinkFailed);
    Assert(!shrinkFailed, "HQ shrink failed");
    AssertThis(0);
    return result == MM_DATA_OK;
}

/***************************************************************************
    If the block is packed, unpack it.
***************************************************************************/
bool DataBlock::FUnpackData(void)
{
    AssertThis(0);
    const bool ok = mm_owned_block_code(&_state, 0, nullptr, KauaiDecodeBlockHandle, nullptr) == MM_DATA_OK;
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Return the amount of memory the block is using (roughly).
***************************************************************************/
LegacyLong DataBlock::CbMem(void)
{
    AssertThis(0);
    return _state.view.kind == MM_BLOCK_MEMORY ? _state.view.capacity : 0;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a DataBlock.
***************************************************************************/
void DataBlock::AssertValid(LegacyUlong grfblck)
{
    DataBlock_PAR::AssertValid(0);
    Assert(mm_owned_block_check(&_state) == MM_DATA_OK, "invalid owning block");
    if (_state.view.kind == MM_BLOCK_FILE)
    {
        FileLocation file = {static_cast<PFileObject>(_state.file), static_cast<LegacyLong>(_state.view.start), static_cast<LegacyLong>(_state.view.length)};
        AssertPo(&file, (grfblck & fblckReadable) ? ffloReadable : 0);
        Assert(!(grfblck & fblckMemory) || (grfblck & fblckFile), "block should be memory based");
    }
    else if (_state.view.kind == MM_BLOCK_MEMORY)
    {
        Assert(!(grfblck & fblckFile) || (grfblck & fblckMemory), "block should be file based");
        AssertHq(_state.view.memory);
        Assert(_state.view.capacity == static_cast<MMU32>(CbOfHq(_state.view.memory)), "wrong block capacity");
    }
    Assert(_state.view.packed || !(grfblck & fblckPacked), "block should be packed");
    Assert(!_state.view.packed || !(grfblck & fblckUnpacked), "block should be unpacked");
}

/***************************************************************************
    Mark memory for the DataBlock.
***************************************************************************/
void DataBlock::MarkMem(void)
{
    AssertValid(0);
    DataBlock_PAR::MarkMem();
    MarkHq(_state.view.memory);
}
#endif // DEBUG

/***************************************************************************
    Constructor for a file based message sink.
***************************************************************************/
MessageSinkFile::MessageSinkFile(PFileObject pfil)
{
    AssertNilOrPo(pfil, 0);

    _fError = fFalse;
    _pfil = pvNil;
    _fpCur = 0;
    if (pvNil != pfil)
        SetFile(pfil);
}

/***************************************************************************
    Destructor for a file based message sink.
***************************************************************************/
MessageSinkFile::~MessageSinkFile(void)
{
    AssertThis(0);
    ReleasePpo(&_pfil);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a MessageSinkFile.
***************************************************************************/
void MessageSinkFile::AssertValid(LegacyUlong grf)
{
    MessageSinkFile_PAR::AssertValid(0);
    AssertNilOrPo(_pfil, 0);
    Assert(_fError || pvNil == _pfil || _fpCur == _pfil->FpMac() || _pfil->ElError() != elNil, "bad _fpCur");
}
#endif // DEBUG

/***************************************************************************
    Set the current file to use for the MessageSinkFile.
***************************************************************************/
void MessageSinkFile::SetFile(PFileObject pfil)
{
    AssertThis(0);
    AssertNilOrPo(pfil, 0);

    _fError = fFalse;
    if (pfil != pvNil)
        pfil->AddRef();
    ReleasePpo(&_pfil);
    _pfil = pfil;
    _fpCur = 0;
    if (pvNil != _pfil)
    {
        _fError |= !_pfil->FSetFpMac(0);
#ifdef UNICODE
        wchar chw = kchwUnicode;
        _fError |= !_pfil->FWriteRgbSeq(&chw, size(wchar), &_fpCur);
#endif // UNICODE
    }
}

/***************************************************************************
    Return the output file and give the caller our reference count on it.
***************************************************************************/
PFileObject MessageSinkFile::PfilRelease(void)
{
    AssertThis(0);
    PFileObject pfil = _pfil;
    _pfil = pvNil;
    return pfil;
}

/***************************************************************************
    Dump a line to the file.
***************************************************************************/
void MessageSinkFile::ReportLine(PZString psz)
{
    AssertThis(0);
    AssertNilOrPo(_pfil, 0);
    achar rgch[2] = {kchReturn, kchLineFeed};

    Report(psz);
    if (pvNil != _pfil)
    {
        _fError |= !_pfil->FWriteRgbSeq(rgch, MacWin(size(achar), 2 * size(achar)), &_fpCur);
    }
}

/***************************************************************************
    Dump some text to the file.
***************************************************************************/
void MessageSinkFile::Report(PZString psz)
{
    AssertThis(0);
    AssertNilOrPo(_pfil, 0);

    if (pvNil == _pfil)
    {
        SetFile(FileObject::PfilCreateTemp());
        if (pvNil == _pfil)
        {
            _fError = fTrue;
            return;
        }
    }

    _fError |= !_pfil->FWriteRgbSeq(psz, CchSz(psz) * size(achar), &_fpCur);
}

/***************************************************************************
    Return whether there has been an error writing to this message sink.
***************************************************************************/
bool MessageSinkFile::FError(void)
{
    AssertThis(0);
    return _fError;
}
