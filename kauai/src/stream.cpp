/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Stream classes.  MemoryByteStream is totally in memory.  FileByteStream allows a stream
    to have pieces in files and other pieces in memory.

***************************************************************************/
#include "util.h"
#include "handle_services.h"
ASSERTNAME

RTCLASS(MemoryByteStream)
RTCLASS(FileByteStream)

/***************************************************************************
    Constructor for an in-memory byte stream.
***************************************************************************/
MemoryByteStream::MemoryByteStream(void)
{
    const auto allocator = Group::CollectionAllocator();
    AssertDo(mm_byte_buffer_init(&_buffer, &allocator) == MM_STORAGE_OK, "invalid byte stream allocator");
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Destructor for an in-memory byte stream.
***************************************************************************/
MemoryByteStream::~MemoryByteStream(void)
{
    AssertThis(fobjAssertFull);
    AssertDo(mm_storage_dispose(&_buffer.storage) == MM_STORAGE_OK, "freeing locked byte stream");
}

/***************************************************************************
    Set the amount to grow by.
***************************************************************************/
void MemoryByteStream::SetMinGrow(LegacyLong cb)
{
    AssertThis(0);
    AssertIn(cb, 0, kcbMax);
    _buffer.min_grow = cb;
}

/***************************************************************************
    Make sure there is at least cb bytes of space.  If fShrink is true,
    the amount of memory allocated by the MemoryByteStream may decrease.
***************************************************************************/
bool MemoryByteStream::FEnsureSpace(LegacyLong cb, bool fShrink)
{
    AssertThis(0);
    if (cb < 0 || static_cast<MMU32>(cb) >= MM_STORAGE_LIMIT - _buffer.length) return false;
    return _FEnsureSize(_buffer.length + cb, fShrink);
}

/***************************************************************************
    Return a locked pointer into the byte stream.  The stream is stored
    contiguously.
***************************************************************************/
void *MemoryByteStream::PvLock(LegacyLong ib)
{
    AssertThis(0);
    AssertIn(ib, 0, static_cast<LegacyLong>(_buffer.length) + 1);
    if (!_buffer.storage.buffers[0].data) return nullptr;
    if (mm_storage_lock(&_buffer.storage, 0) != MM_STORAGE_OK) return nullptr;
    return _buffer.storage.buffers[0].data + ib;
}

/***************************************************************************
    Unlock the stream.
***************************************************************************/
void MemoryByteStream::Unlock(void)
{
    AssertThis(0);
    if (!_buffer.storage.buffers[0].data) return;
    AssertDo(mm_storage_unlock(&_buffer.storage, 0) == MM_STORAGE_OK, "byte stream not locked");
}

/***************************************************************************
    Fetch some bytes from the stream.
***************************************************************************/
void MemoryByteStream::FetchRgb(LegacyLong ib, LegacyLong cb, void *prgb)
{
    AssertThis(0);
    AssertDo(mm_byte_buffer_read(&_buffer, ib, prgb, cb) == MM_STORAGE_OK, "invalid stream read");
}

/***************************************************************************
    Replace the range [ib,ib + cbDel) with cbIns bytes from prgb.  If cbIns
    is zero, prgb may be nil.
***************************************************************************/
bool MemoryByteStream::FReplace(void *prgb, LegacyLong cbIns, LegacyLong ib, LegacyLong cbDel)
{
    AssertThis(fobjAssertFull);
    const bool ok = mm_byte_buffer_replace(&_buffer, ib, cbDel, prgb, cbIns) == MM_STORAGE_OK;
    AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************
    Write the byte stream to a file.
***************************************************************************/
bool MemoryByteStream::FWriteRgb(PFileLocation pflo, LegacyLong ib)
{
    AssertThis(0);
    AssertPo(pflo, 0);
    DataBlock blck(pflo);

    return FWriteRgb(&blck, ib);
}

/***************************************************************************
    Write the byte stream to a block.
***************************************************************************/
namespace {
int WriteByteStreamBlock(void *context, MMU32 offset, const void *bytes, size_t count)
{
    if (offset > 0x7fffffffUL || count > 0x7fffffffUL) return 0;
    return static_cast<PDataBlock>(context)->FWriteRgb(const_cast<void *>(bytes), count, offset);
}
} // namespace
bool MemoryByteStream::FWriteRgb(PDataBlock pblck, LegacyLong ib)
{
    AssertThis(fobjAssertFull);
    AssertPo(pblck, 0);
    const MMStorageWriter writer = {pblck, static_cast<MMU32>(pblck->Cb()), WriteByteStreamBlock};
    return mm_byte_buffer_write(&_buffer, ib, &writer) == MM_STORAGE_OK;
}

/***************************************************************************
    Ensure at least cbMin bytes, retaining the legacy growth hint even for
    nonzero shrink requests.
***************************************************************************/
bool MemoryByteStream::_FEnsureSize(LegacyLong cbMin, bool fShrink)
{
    AssertThis(fobjAssertFull);
    return mm_byte_buffer_reserve(&_buffer, cbMin, fShrink) == MM_STORAGE_OK;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a byte stream (FileByteStream).
***************************************************************************/
void MemoryByteStream::AssertValid(LegacyUlong grf)
{
    MemoryByteStream_PAR::AssertValid(grf);
    Assert(_buffer.length < MM_STORAGE_LIMIT && _buffer.min_grow < MM_STORAGE_LIMIT, "invalid byte stream size");
    const auto &buffer = _buffer.storage.buffers[0];
    Assert(_buffer.length <= buffer.capacity, "byte stream area too small");
    if (buffer.data) AssertPvAlloced(buffer.data, buffer.capacity);
}

/***************************************************************************
    Mark memory for the MemoryByteStream.
***************************************************************************/
void MemoryByteStream::MarkMem(void)
{
    AssertValid(fobjAssertFull);
    MemoryByteStream_PAR::MarkMem();
    MarkPv(_buffer.storage.buffers[0].data);
}
#endif // DEBUG

/***************************************************************************
    Constructor for a stream.
***************************************************************************/
FileByteStream::FileByteStream(void)
{
    const auto allocator = Group::CollectionAllocator();
    const MMPieceFiles files = {this, _RetainFile, _ReleaseFile, _CreateTemp, _ReadFile, _WriteFile, _CopyFile};
    int trash = 0;
#ifdef DEBUG
    trash = 1;
#endif
    AssertDo(mm_piece_table_init(&_pieces, &allocator, &files, trash) == MM_DATA_OK, "invalid piece table");
    AssertThis(fobjAssertFull);
}
FileByteStream::~FileByteStream(void)
{
    AssertThis(fobjAssertFull);
    AssertDo(mm_piece_table_dispose(&_pieces) == MM_DATA_OK, "freeing locked piece table");
}
namespace {
FileLocation NativePiece(const MMFilePiece &piece)
{
    return {static_cast<PFileObject>(piece.file), static_cast<LegacyLong>(piece.offset), static_cast<LegacyLong>(piece.length)};
}
}
void FileByteStream::_RetainFile(void *, void *file) { static_cast<PFileObject>(file)->AddRef(); }
void FileByteStream::_ReleaseFile(void *, void *file) { static_cast<PFileObject>(file)->Release(); }
void *FileByteStream::_CreateTemp(void *) { return FileObject::PfilCreateTemp(); }
int FileByteStream::_ReadFile(void *, const MMFilePiece *piece, void *output)
{
    auto location = NativePiece(*piece);
    const bool ok = location.FRead(output);
    if (!ok) Warn("read failed in fetch");
    return ok;
}
int FileByteStream::_WriteFile(void *, const MMFilePiece *piece, const void *input)
{
    auto location = NativePiece(*piece);
    return location.FWrite(const_cast<void *>(input));
}
int FileByteStream::_CopyFile(void *, const MMFilePiece *source, const MMFilePiece *destination)
{
    auto from = NativePiece(*source), to = NativePiece(*destination);
    return from.FCopy(&to);
}
bool FileByteStream::FReplace(void *bytes, LegacyLong count, LegacyLong offset, LegacyLong remove)
{
    AssertThis(fobjAssertFull);
    const bool ok = mm_piece_table_replace(&_pieces, offset, remove, bytes, count) == MM_DATA_OK;
    AssertThis(fobjAssertFull);
    return ok;
}
bool FileByteStream::FReplaceFlo(PFileLocation location, bool copy, LegacyLong offset, LegacyLong remove)
{
    AssertThis(fobjAssertFull);
    AssertPo(location, ffloReadable);
    const MMFilePiece piece = {location->pfil, static_cast<MMU32>(location->fp), static_cast<MMU32>(location->cb)};
    const bool ok = mm_piece_table_replace_file(&_pieces, offset, remove, &piece, copy) == MM_DATA_OK;
    AssertThis(fobjAssertFull);
    return ok;
}
bool FileByteStream::FReplaceBsf(PFileByteStream source, LegacyLong from, LegacyLong count, LegacyLong offset, LegacyLong remove)
{
    AssertThis(fobjAssertFull);
    AssertPo(source, 0);
    const bool ok = mm_piece_table_replace_table(&_pieces, offset, remove, &source->_pieces, from, count) == MM_DATA_OK;
    AssertThis(fobjAssertFull);
    return ok;
}
void FileByteStream::FetchRgb(LegacyLong offset, LegacyLong count, void *output)
{
    AssertThis(fobjAssertFull);
    const int result = mm_piece_table_read(&_pieces, offset, output, count);
    Assert(result == MM_DATA_OK || result == MM_DATA_IO, "invalid stream fetch");
}
bool FileByteStream::FWriteRgb(PFileLocation location, LegacyLong offset)
{
    AssertThis(0);
    AssertPo(location, 0);
    DataBlock block(location);
    return FWriteRgb(&block, offset);
}
bool FileByteStream::FWriteRgb(PDataBlock block, LegacyLong offset)
{
    AssertThis(fobjAssertFull);
    AssertPo(block, 0);
    const int result = mm_piece_table_write(&_pieces, offset, &block->_state, KauaiCopyAllocator());
    if (result == MM_DATA_ARGUMENT || result == MM_DATA_PACKED) Bug("invalid stream write window");
    return result == MM_DATA_OK;
}
bool FileByteStream::FCompact(void)
{
    AssertThis(fobjAssertFull);
    const bool ok = mm_piece_table_compact(&_pieces) == MM_DATA_OK;
    AssertThis(fobjAssertFull);
    return ok;
}
#ifdef DEBUG
void FileByteStream::AssertValid(LegacyUlong flags)
{
    FileByteStream_PAR::AssertValid(flags);
    Assert(_pieces.length < MM_STORAGE_LIMIT, "invalid piece table length");
    for (const auto &buffer : _pieces.storage.buffers)
        if (buffer.data) AssertPvAlloced(buffer.data, buffer.capacity);
    if (!(flags & fobjAssertFull)) return;
    MMU32 total = 0;
    for (MMU32 i = 0; i < _pieces.group.count; ++i)
    {
        MMFilePiece piece;
        AssertDo(mm_piece_table_get(&_pieces, i, &piece, nullptr) == MM_DATA_OK, "invalid piece");
        if (piece.file) AssertPo(static_cast<PFileObject>(piece.file), 0);
        total += piece.length;
    }
    Assert(total == _pieces.length, "bad piece table length");
}
void FileByteStream::MarkMem(void)
{
    AssertThis(fobjAssertFull);
    FileByteStream_PAR::MarkMem();
    MarkPv(_pieces.storage.buffers[0].data);
    MarkPv(_pieces.storage.buffers[1].data);
}
#endif
