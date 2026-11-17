/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Shared filename navigation and enumeration over native path services.

***************************************************************************/
#include "util.h"
#include "mm/filename_path.h"
#include "mm/filename_naming.h"
#include "mm/filename_case.h"
ASSERTNAME
namespace Group { MMStorageAllocator CollectionAllocator(); }

namespace {
MMFilenameNamingState filenameNaming{};
void FilenameCopyCheck(void *, const void *source, void *destination, size_t bytes)
{
    AssertPvCb(const_cast<void *>(source), bytes); AssertPvCb(destination, bytes);
    Assert(static_cast<const byte *>(source) + bytes <= destination ||
           static_cast<byte *>(destination) + bytes <= source, "blocks overlap");
}
MMStringValue FilenameStringValue(PString string)
{
    MMStringValue value{string->Pst(), sizeof(achar), nullptr, FilenameCopyCheck};
    return value;
}
MMFilenameValueHost FilenameValueHost()
{
    MMFilenameValueHost host{};
    host.upper = [](void *, unsigned ch) -> unsigned { return static_cast<byte>(ChsUpper(static_cast<schar>(ch))); };
    host.separator = [](void *, unsigned ch) -> int { return KauaiFiles::IsSeparator(static_cast<achar>(ch)); };
    host.failed = [](void *) { PushErc(ercFniGeneral); };
#ifdef DEBUG
    static_assert(ffniFile == (MM_FILENAME_FILE << 16) && ffniDir == (MM_FILENAME_DIR << 16) &&
                  ffniEmpty == (MM_FILENAME_EMPTY << 16), "native filename validation flags");
    host.validate = [](void *, const MMFilenameValue *value, int flags) {
        auto *owner = static_cast<Filename *>(value->owner);
        if (flags == MM_FILENAME_BASE) { AssertBasePo(owner, 0); }
        else { AssertPo(owner, static_cast<LegacyUlong>(flags) << 16); }
    };
    host.assertion = [](void *, int code) {
        switch (code) {
        case MM_FILENAME_BAD_TYPE: Assert(fFalse, "Bad FileType"); break;
        case MM_FILENAME_LEAF_TYPE: Assert(fFalse, "ftg doesn't match pstn"); break;
        case MM_FILENAME_BAD_PATH: Assert(fFalse, "bad fni"); break;
        default: Assert(fFalse, 0); break;
        }
    };
#endif
    return host;
}
MMFilenamePathHost FilenamePathHost()
{
    static_assert(ffniCreateDir == MM_FILENAME_CREATE_DIR && ffniMoveToDir == MM_FILENAME_MOVE_TO_DIR,
                  "native filename navigation flags");
    static_assert(tNo == MM_FILENAME_NO && tYes == MM_FILENAME_YES && tMaybe == MM_FILENAME_MAYBE,
                  "native filename status contract");
    static_assert(static_cast<int>(KauaiFiles::Status::Found) == MM_FILENAME_FOUND &&
                  static_cast<int>(KauaiFiles::Status::Missing) == MM_FILENAME_MISSING &&
                  static_cast<int>(KauaiFiles::Status::Error) == MM_FILENAME_INFO_ERROR, "native filename info contract");
    MMFilenamePathHost host{}; host.value = FilenameValueHost();
    host.full_path = [](void *, const void *input, void *output, MMI32 capacity) -> MMI32 {
        return static_cast<MMI32>(KauaiFiles::FullPath(static_cast<const achar *>(input), static_cast<achar *>(output), capacity));
    };
    host.find_path = [](void *, const void *input, const void *search, void *output, MMI32 capacity) -> MMI32 {
        return static_cast<MMI32>(KauaiFiles::FindInPath(static_cast<const achar *>(input), static_cast<const achar *>(search),
                                                      static_cast<achar *>(output), capacity));
    };
    host.separator = [](void *) -> unsigned { return KauaiFiles::Separator(); };
    host.info = [](void *, const void *path, MMFilenameInfo *out) -> int {
        KauaiFiles::PathInfo info;
        const auto status = KauaiFiles::Info(static_cast<const achar *>(path), &info);
        out->directory = info.directory; out->hidden = info.hidden; out->system = info.system;
        return static_cast<int>(status);
    };
    host.remove = [](void *, const void *path) -> int { return KauaiFiles::Delete(static_cast<const achar *>(path)); };
    host.rename = [](void *, const void *a, const void *b) -> int {
        return KauaiFiles::Rename(static_cast<const achar *>(a), static_cast<const achar *>(b));
    };
    host.make_directory = [](void *, const void *path) -> int { return KauaiFiles::MakeDirectory(static_cast<const achar *>(path)); };
    host.equal = [](void *, const void *a, const void *b) -> int {
        return KauaiFiles::EqualPaths(static_cast<const achar *>(a), static_cast<const achar *>(b));
    };
    host.volume_kinds = [](void *, const void *path) -> MMU32 { return KauaiFiles::VolumeKinds(static_cast<const achar *>(path)); };
    host.copy_name = [](void *, const MMFilenameValue *destination, const MMFilenameValue *source) {
        *static_cast<Filename *>(destination->owner) = *static_cast<Filename *>(source->owner);
    };
    host.issue = [](void *, const MMFilenameValue *, int code) {
        switch (code) {
        case MM_FILENAME_GENERAL: PushErc(ercFniGeneral); break;
        case MM_FILENAME_DELETE: PushErc(ercFniDelete); break;
        case MM_FILENAME_RENAME: PushErc(ercFniRename); break;
        case MM_FILENAME_MISMATCH: PushErc(ercFniMismatch); break;
        case MM_FILENAME_HIDDEN: PushErc(ercFniHidden); break;
        case MM_FILENAME_DIR_CREATE: PushErc(ercFniDirCreate); break;
        }
    };
#ifdef DEBUG
    host.is_open = [](void *, const MMFilenameValue *value) -> int {
        return FileObject::PfilFromFni(static_cast<Filename *>(value->owner)) != nullptr;
    };
    host.assertion = [](void *, int code) {
        if (code == MM_FILENAME_PATH_OPEN) { Assert(fFalse, "file is open"); }
        else { Assert(fFalse, 0); }
    };
#endif
    return host;
}
MMFilenameNamingHost FilenameNamingHost()
{
    MMFilenameNamingHost host{}; host.path = FilenamePathHost();
    host.clock = [](void *) -> MMU32 { return TsCurrentSystem(); };
    host.temp_path = [](void *, void *output, MMI32 capacity) -> MMI32 {
        return static_cast<MMI32>(KauaiFiles::TempPath(static_cast<achar *>(output), capacity));
    };
    return host;
}
MMFilenameCaseHost FilenameCaseHost()
{
    static_assert(static_cast<int>(KauaiFiles::Iteration::Entry) == MM_FILENAME_CASE_ENTRY &&
                  static_cast<int>(KauaiFiles::Iteration::End) == MM_FILENAME_CASE_END &&
                  static_cast<int>(KauaiFiles::Iteration::Error) == MM_FILENAME_CASE_ERROR, "native case iteration contract");
    static_assert(sizeof(KauaiFiles::DirectoryEntry{}.name) == MM_FILENAME_CASE_NAME_CAPACITY * sizeof(achar),
                  "native case entry profile");
    MMFilenameCaseHost host{}; host.path = FilenamePathHost();
#ifndef _WIN32
    host.sensitive = 1;
#endif
    host.first = [](void *, const void *pattern, void **directory, void *name, MMI32 capacity) -> int {
        Assert(capacity == MM_FILENAME_CASE_NAME_CAPACITY, "case entry capacity");
        KauaiFiles::DirectoryEntry entry;
        const auto status = KauaiFiles::First(static_cast<const achar *>(pattern), directory, &entry);
        if (status == KauaiFiles::Iteration::Entry) CopyPb(entry.name, name, sizeof(entry.name));
        return static_cast<int>(status);
    };
    host.next = [](void *, void *directory, void *name, MMI32 capacity) -> int {
        Assert(capacity == MM_FILENAME_CASE_NAME_CAPACITY, "case entry capacity");
        KauaiFiles::DirectoryEntry entry;
        const auto status = KauaiFiles::Next(directory, &entry);
        if (status == KauaiFiles::Iteration::Entry) CopyPb(entry.name, name, sizeof(entry.name));
        return static_cast<int>(status);
    };
    host.close = [](void *, void *directory) { KauaiFiles::CloseDirectory(directory); };
    host.name_length = [](void *, const void *name) -> MMI32 {
        return CchSz(const_cast<achar *>(static_cast<const achar *>(name)));
    };
    return host;
}
}
MMFilenameValue Filename::_ValueView()
{
    static_assert(sizeof(MMFilenameState) == sizeof(FileType), "native filename type profile");
    static_assert(ftgNil == MM_FILENAME_NIL && kftgDir == MM_FILENAME_DIRECTORY, "native filename types");
    MMFilenameValue value{&_nameState, FilenameStringValue(&_stnFile), this};
    return value;
}

// This is the FileType to use for temp files - clients may set this to whatever
// they want.
FileType vftgTemp = kftgTemp;

bool Filename::FResolveCase(void)
{
    const auto value = _ValueView(); const auto host = FilenameCaseHost();
    return mm_filename_resolve_case(&value, &host) != 0;
}

Filename _fniTemp;

RTCLASS(Filename)
RTCLASS(FileNameEnumerator)

/***************************************************************************
    Sets the fni to nil values.
***************************************************************************/
void Filename::SetNil(void)
{
    const auto value = _ValueView(); const auto host = FilenameValueHost();
    mm_filename_value_clear(&value, &host);
}

/***************************************************************************
    Constructor for fni class.
***************************************************************************/
Filename::Filename(void)
{
    SetNil();
}

/***************************************************************************
    Builds the fni from the path.
***************************************************************************/
bool Filename::FBuildFromPath(PString pstn, FileType ftgDef)
{
    AssertPo(pstn, 0);
    const auto value = _ValueView(); const auto host = FilenamePathHost();
    const auto input = FilenameStringValue(pstn);
    return mm_filename_build(&value, &host, &input, ftgDef) != 0;
}

/******************************************************************************
    Will attempt to build an Filename with the given filename.  Uses the
    Native path-search rules (SearchPath on Windows, PATH-style on POSIX).

    Arguments:
        PString pstn ** the filename to look for

    Returns: fTrue if it could find the file
******************************************************************************/
bool Filename::FSearchInPath(PString pstn, PString pstnEnv)
{
    AssertPo(pstn, 0); AssertNilOrPo(pstnEnv, 0);
    const auto value = _ValueView(); const auto host = FilenamePathHost();
    const auto input = FilenameStringValue(pstn);
    MMStringValue search{}; if (pstnEnv) search = FilenameStringValue(pstnEnv);
    return mm_filename_search(&value, &host, &input, pstnEnv ? &search : nullptr) != 0;
}

/***************************************************************************
    Get a unique filename in the directory currently indicated by the fni.
***************************************************************************/
bool Filename::FGetUnique(FileType ftg)
{
    const auto value = _ValueView(); const auto host = FilenameNamingHost();
    return mm_filename_unique(&value, &host, &filenameNaming, ftg) != 0;
}

/***************************************************************************
    Get a temporary fni.
***************************************************************************/
bool Filename::FGetTemp(void)
{
    const auto value = _ValueView(), cached = _fniTemp._ValueView(); const auto host = FilenameNamingHost();
    return mm_filename_temp(&value, &host, &filenameNaming, &cached, vftgTemp) != 0;
}

/***************************************************************************
    Return the file type of the fni.
***************************************************************************/
FileType Filename::Ftg(void)
{
    AssertThis(0);
    return _nameState.type;
}

/***************************************************************************
    Return the volume kind for the given fni.
***************************************************************************/
LegacyUlong Filename::Grfvk(void)
{
    const auto value = _ValueView(); const auto host = FilenamePathHost();
    return mm_filename_volume(&value, &host);
}

/***************************************************************************
    Set the leaf to the given string and type.
***************************************************************************/
bool Filename::FSetLeaf(PString pstn, FileType ftg)
{
    AssertNilOrPo(pstn, 0);
    const auto value = _ValueView(); const auto host = FilenameValueHost();
    return mm_filename_set_leaf(&value, &host, pstn ? pstn->Prgch() : nullptr, pstn ? pstn->Cch() : 0, ftg) != 0;
}

/******************************************************************************
    Changes just the FileType of the Filename, leaving the file path and filename alone
    (but does change the extension). Returns: fTrue if it succeeds
******************************************************************************/
bool Filename::FChangeFtg(FileType ftg)
{
    const auto value = _ValueView(); const auto host = FilenameValueHost();
    return mm_filename_change_type(&value, &host, ftg) != 0;
}

/***************************************************************************
    Get the leaf name.
***************************************************************************/
void Filename::GetLeaf(PString pstn)
{
    AssertPo(pstn, 0);
    const auto value = _ValueView(); const auto host = FilenameValueHost();
    const auto output = FilenameStringValue(pstn);
    mm_filename_get_leaf(&value, &host, &output);
}

/***************************************************************************
    Get a string representing the path of the fni.
***************************************************************************/
void Filename::GetStnPath(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    *pstn = _stnFile;
}

/***************************************************************************
    Determines if the file/directory exists.  Returns tMaybe on error or
    if the fni type (file or dir) doesn't match the disk object of the
    same name.
***************************************************************************/
tribool Filename::TExists(void)
{
    const auto value = _ValueView(); const auto host = FilenamePathHost();
    return static_cast<tribool>(mm_filename_exists(&value, &host));
}

/***************************************************************************
    Delete the physical file.  Should not be open.
***************************************************************************/
bool Filename::FDelete(void)
{
    const auto value = _ValueView(); const auto host = FilenamePathHost();
    return mm_filename_delete(&value, &host) != 0;
}

/***************************************************************************
    Renames the file indicated by this to *pfni.
***************************************************************************/
bool Filename::FRename(Filename *pfni)
{
    const auto value = _ValueView(), destination = pfni->_ValueView();
    const auto host = FilenamePathHost();
    return mm_filename_rename(&value, &host, &destination) != 0;
}

/***************************************************************************
    Compare two fni's for equality.
***************************************************************************/
bool Filename::FEqual(Filename *pfni)
{
    const auto value = _ValueView(), other = pfni->_ValueView();
    const auto host = FilenamePathHost();
    return mm_filename_equal(&value, &host, &other) != 0;
}

/***************************************************************************
    Return whether the fni refers to a directory.
***************************************************************************/
bool Filename::FDir(void)
{
    AssertThis(0);
    return _nameState.type == kftgDir;
}

/***************************************************************************
    Return whether the directory portions of the fni's are the same.
***************************************************************************/
bool Filename::FSameDir(Filename *pfni)
{
    Filename a, b;
    const auto value = _ValueView(), other = pfni->_ValueView(), scratchA = a._ValueView(), scratchB = b._ValueView();
    const auto host = FilenamePathHost();
    return mm_filename_same_directory(&value, &host, &other, &scratchA, &scratchB) != 0;
}

/***************************************************************************
    Determine if the directory pstn in fni exists, optionally creating it
    and/or moving into it.  Specify ffniCreateDir to create it if it
    doesn't exist.  Specify ffniMoveTo to make the fni refer to it.
***************************************************************************/
bool Filename::FDownDir(PString pstn, LegacyUlong grffni)
{
    AssertPo(pstn, 0);
    Filename temporary;
    const auto value = _ValueView(), scratch = temporary._ValueView();
    const auto leaf = FilenameStringValue(pstn); const auto host = FilenamePathHost();
    return mm_filename_down(&value, &host, &leaf, grffni, &scratch) != 0;
}

/***************************************************************************
    Gets the lowest directory name (if pstn is not nil) and optionally
    moves the fni up a level (if ffniMoveToDir is specified).
***************************************************************************/
bool Filename::FUpDir(PString pstn, LegacyUlong grffni)
{
    AssertNilOrPo(pstn, 0);
    const auto value = _ValueView(); const auto host = FilenamePathHost();
    MMStringValue leaf{}; if (pstn) leaf = FilenameStringValue(pstn);
    return mm_filename_up(&value, &host, pstn ? &leaf : nullptr, grffni) != 0;
}

#ifdef DEBUG
/***************************************************************************
    Assert validity of the Filename.
***************************************************************************/
void Filename::AssertValid(LegacyUlong grffni)
{
    Filename_PAR::AssertValid(0);
    AssertPo(&_stnFile, 0);

    ZString szT;
    LegacyLong cch;
    PZString pszT;

    if (grffni == 0)
        grffni = ffniEmpty | ffniDir | ffniFile;

    if (_nameState.type == ftgNil)
    {
        Assert(grffni & ffniEmpty, "unexpected empty");
        Assert(_stnFile.Cch() == 0, "named empty?");
        return;
    }

    if ((cch = KauaiFiles::FullPath(_stnFile.Psz(), szT, kcchMaxSz, &pszT)) == 0 || cch > kcchMaxSz ||
        !KauaiFiles::EqualPaths(_stnFile.Psz(), szT))
    {
        Bug("bad fni");
        return;
    }

    if (_nameState.type == kftgDir)
    {
        Assert(grffni & ffniDir, "unexpected dir");
        Assert(KauaiFiles::IsSeparator(szT[cch - 1]), "expected trailing slash");
        Assert(pszT == NULL, "unexpected filename");
    }
    else
    {
        Assert(grffni & ffniFile, "unexpected file");
        Assert(pszT >= szT && pszT < szT + cch, "expected filename");
    }
}
#endif // DEBUG

/***************************************************************************
    Find the length of the file extension on the fni (including the period).
    Allow up to four characters for the extension (plus one for the
    period).
***************************************************************************/
LegacyLong Filename::_CchExt(void)
{
    const auto value = _ValueView(); const auto host = FilenameValueHost();
    return mm_filename_extension_length(&value, &host);
}

/***************************************************************************
    Set the ftg from the file name.
***************************************************************************/
void Filename::_SetFtgFromName(void)
{
    const auto value = _ValueView(); const auto host = FilenameValueHost();
    mm_filename_derive_type(&value, &host);
}

/***************************************************************************
    Change the leaf of the fni.
***************************************************************************/
bool Filename::_FChangeLeaf(PString pstn)
{
    AssertNilOrPo(pstn, 0);
    const auto value = _ValueView(); const auto host = FilenameValueHost();
    return mm_filename_change_leaf(&value, &host, pstn ? pstn->Prgch() : nullptr, pstn ? pstn->Cch() : 0) != 0;
}

namespace {
void *ResizeEnumTypes(void *, void *old, MMU32 oldBytes, MMU32 bytes, int priority)
{
    void *result = old;
    const auto mpr = priority == MM_STORAGE_SPEED ? mprForSpeed : mprNormal;
    const bool ok = old ? _FResizePpvDebug(&result,bytes,oldBytes,fmemNil,mpr,&vdmglob.dmaglPv) :
                         FAllocPv(&result,bytes,fmemNil,mpr);
    return ok ? result : nullptr;
}
void FreeEnumTypes(void *, void *data, MMU32) { FreePpv(&data); }
}
MMFilenameEnumFrame FileNameEnumerator::_EnumFrame()
{
    return {&_fesCur, &_fesCur.cursor, _fesCur.fni._ValueView(), _fesCur.entry};
}
MMFilenameEnumHost FileNameEnumerator::_EnumHost()
{
    static_assert(kcftgFneBase == MM_FILENAME_ENUM_INLINE && sizeof(FileType) == sizeof(MMI32), "native enum filter profile");
    static_assert(ffneRecurse == MM_FILENAME_ENUM_RECURSE && ffnePre == MM_FILENAME_ENUM_PRE &&
                  ffnePost == MM_FILENAME_ENUM_POST && ffneSkipDir == MM_FILENAME_ENUM_SKIP, "native enum flags");
    static_assert(sizeof(LegacyUlong) == sizeof(MMU32), "native enum flag width");
    MMFilenameEnumHost host{}; host.path = FilenamePathHost(); host.context = this;
    host.first = [](void *, const void *pattern, void **iterator, void *name, MMI32 capacity, MMFilenameEnumInfo *out) -> int {
        Assert(capacity == MM_FILENAME_CASE_NAME_CAPACITY, "enumeration entry capacity");
        KauaiFiles::DirectoryEntry entry;
        const auto status = KauaiFiles::First(static_cast<const achar *>(pattern),iterator,&entry);
        if (status == KauaiFiles::Iteration::Entry) {
            CopyPb(entry.name,name,sizeof(entry.name));
            *out = {entry.info.directory,entry.info.hidden,entry.info.system,entry.info.link};
        }
        return static_cast<int>(status);
    };
    host.next = [](void *, void *iterator, void *name, MMI32 capacity, MMFilenameEnumInfo *out) -> int {
        Assert(capacity == MM_FILENAME_CASE_NAME_CAPACITY, "enumeration entry capacity");
        KauaiFiles::DirectoryEntry entry; const auto status = KauaiFiles::Next(iterator,&entry);
        if (status == KauaiFiles::Iteration::Entry) {
            CopyPb(entry.name,name,sizeof(entry.name));
            *out = {entry.info.directory,entry.info.hidden,entry.info.system,entry.info.link};
        }
        return static_cast<int>(status);
    };
    host.close = [](void *, void *iterator) { KauaiFiles::CloseDirectory(iterator); };
    host.roots = [](void *) -> MMU32 { return KauaiFiles::RootMask(); };
    host.root_path = [](void *, unsigned index, void *name, MMI32 capacity) -> int {
        return KauaiFiles::RootPath(index,static_cast<achar *>(name),capacity);
    };
    host.name_length = [](void *, const void *name) -> MMI32 { return CchSz(const_cast<achar *>(static_cast<const achar *>(name))); };
    host.issue = [](void *) { PushErc(ercFneGeneral); };
#ifdef DEBUG
    host.validate = [](void *context, int base) {
        auto *owner = static_cast<FileNameEnumerator *>(context);
        if (base) { AssertBasePo(owner,0); } else { AssertPo(owner,0); }
    };
    host.assertion = [](void *, int code) {
        if (code == MM_FILENAME_ENUM_UNINITIALIZED) { Bug("must initialize the FileNameEnumerator before using it!"); }
        else { Assert(fFalse,0); }
    };
#endif
    return host;
}
FileNameEnumerator::FileNameEnumerator(void)
{
    AssertBaseThis(0);
    const MMStorageAllocator types{nullptr,ResizeEnumTypes,FreeEnumTypes};
    const auto stack = Group::CollectionAllocator();
    int trash = 0;
#ifdef DEBUG
    trash = 1;
#endif
    AssertDo(mm_filename_enum_construct(&_enumeration,sizeof(FES),&types,&stack,trash),"invalid filename enumeration storage");
    AssertThis(0);
}
FileNameEnumerator::~FileNameEnumerator(void) { AssertBaseThis(0); _Free(); }
void FileNameEnumerator::_Free(void)
{
    const auto host = _EnumHost(); const auto frame = _EnumFrame(); mm_filename_enum_free(&_enumeration,&host,&frame);
}
bool FileNameEnumerator::FInit(Filename *directory, FileType *types, LegacyLong count, LegacyUlong flags)
{
    AssertNilOrVarMem(directory); AssertIn(count,0,kcbMax); AssertPvCb(types,LwMul(count,sizeof(FileType)));
    const auto host = _EnumHost(); const auto frame = _EnumFrame();
    MMFilenameValue input{}; if (directory) input = directory->_ValueView();
    return mm_filename_enum_init(&_enumeration,&host,&frame,directory ? &input : nullptr,types,count,flags) != 0;
}
bool FileNameEnumerator::FNextFni(Filename *output, LegacyUlong *flags, LegacyUlong inputFlags)
{
    AssertVarMem(output); AssertNilOrVarMem(flags);
    Filename scratch;
    const auto host = _EnumHost(); const auto frame = _EnumFrame();
    const auto out = output->_ValueView(), temporary = scratch._ValueView();
    return mm_filename_enum_next(&_enumeration,&host,&frame,&out,flags,inputFlags,&temporary) != 0;
}
#ifdef DEBUG
void FileNameEnumerator::AssertValid(LegacyUlong grf)
{
    FileNameEnumerator_PAR::AssertValid(0);
    if (_enumeration.initialized) {
        AssertIn(_enumeration.type_count,0,kcbMax);
        AssertPvCb(_enumeration.types,LwMul(_enumeration.type_count,sizeof(MMI32)));
        Assert((_enumeration.type_count <= MM_FILENAME_ENUM_INLINE) == (_enumeration.types == _enumeration.inline_types),"wrong enumeration types");
    } else Assert(_enumeration.depth == 0 && !_enumeration.stack.buffers[0].data,0);
}
void FileNameEnumerator::MarkMem(void)
{
    AssertValid(0); FileNameEnumerator_PAR::MarkMem();
    if (_enumeration.types != _enumeration.inline_types) MarkPv(_enumeration.types);
    MarkPv(_enumeration.stack.buffers[0].data); MarkPv(_enumeration.stack.buffers[1].data);
}
#endif
