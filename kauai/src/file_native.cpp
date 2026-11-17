/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Shared file lifecycle and error policy over the native file backend.

***************************************************************************/
#include "util.h"
ASSERTNAME

namespace
{
KauaiFiles::FileHandle OpenHandle(const achar *path, bool create, LegacyUlong flags)
{
    KauaiFiles::OpenOptions options;
    options.write = (flags & ffilWriteEnable) != 0;
    options.denyRead = (flags & ffilDenyRead) != 0;
    options.denyWrite = (flags & ffilDenyWrite) != 0;
    options.creation = !create              ? KauaiFiles::Creation::OpenExisting
                       : (flags & ffilTemp) ? KauaiFiles::Creation::CreateExclusive
                                            : KauaiFiles::Creation::CreateAlways;
    return KauaiFiles::Open(path, options);
}
} // namespace

MMFileOwnerHost FileObject::_OwnerHost()
{
    static_assert(ffilWriteEnable == MM_FILE_WRITE && ffilDenyWrite == MM_FILE_DENY_WRITE &&
                  ffilDenyRead == MM_FILE_DENY_READ && kgrffilPerm == MM_FILE_PERMISSIONS &&
                  ffilTemp == MM_FILE_TEMP && ffilMark == MM_FILE_MARK, "native file flag contract");
    static_assert(elNil == MM_FILE_ERROR_NONE && kelWrite == MM_FILE_ERROR_WRITE &&
                  kelRead == MM_FILE_ERROR_READ && kelSeek == MM_FILE_ERROR_SEEK &&
                  kelCritical == MM_FILE_ERROR_CRITICAL, "native file error contract");
    MMFileOwnerHost host{};
    host.context = this;
    host.enter = [](void *c) { static_cast<PFileObject>(c)->_mutx.Enter(); };
    host.leave = [](void *c) { static_cast<PFileObject>(c)->_mutx.Leave(); };
    host.open = [](void *c, int create, MMU32 flags) -> void * {
        return OpenHandle(static_cast<PFileObject>(c)->_fni._stnFile.Psz(), create != 0, flags);
    };
    host.close = [](void *, void *handle) { KauaiFiles::Close(handle); };
    host.flush = [](void *, void *handle) -> int { return KauaiFiles::Flush(handle); };
    host.remove = [](void *c) -> int { return KauaiFiles::Delete(static_cast<PFileObject>(c)->_fni._stnFile.Psz()); };
    host.seek = [](void *, void *handle, MMU32 position) -> int { return KauaiFiles::Seek(handle, position); };
    host.resize = [](void *, void *handle, MMU32 bytes) -> int { return KauaiFiles::Resize(handle, bytes); };
    host.size = [](void *, void *handle, MMU32 *bytes) -> int {
        uint32_t length = 0;
        const bool result = KauaiFiles::Size(handle, &length);
        *bytes = length;
        return result;
    };
    host.read = [](void *, void *handle, void *data, MMU32 bytes) -> int { return KauaiFiles::Read(handle, data, bytes); };
    host.write = [](void *, void *handle, const void *data, MMU32 bytes) -> int { return KauaiFiles::Write(handle, data, bytes); };
    host.issue = [](void *, int code) {
        if (code == MM_FILE_ISSUE_GENERAL) PushErc(ercFileGeneral);
        else if (code == MM_FILE_ISSUE_PERMISSION) PushErc(ercFilePerm);
        else Warn("Deleting temp file failed");
    };
#ifdef DEBUG
    host.validate = [](void *c, int base) {
        auto *file = static_cast<PFileObject>(c);
        if (base) { AssertBasePo(file, 0); }
        else { AssertPo(file, 0); }
    };
    host.arguments = [](void *, int operation, const void *data, MMI32 bytes, MMI32 position) {
        if (operation == MM_FILE_ARGUMENT_RESIZE) { AssertIn(position, 0, kcbMax); }
        else {
            AssertIn(bytes, 0, kcbMax);
            AssertIn(position, 0, klwMax);
            AssertPvCb(const_cast<void *>(data), bytes);
        }
    };
    host.assertion = [](void *, int code) {
        if (code == MM_FILE_CHECK_WRITE) { Assert(fFalse, "can't write to read only file"); }
        else if (code == MM_FILE_CHECK_CREATE) { Assert(fFalse, "can't create an open file"); }
        else { Assert(fFalse, "read past EOF"); }
    };
    host.debug_read = 1;
#endif
    return host;
}

bool FileObject::_FOpen(bool create, LegacyUlong flags)
{
    const auto host = _OwnerHost();
    return mm_file_owner_open(&_file, &host, create, flags) != 0;
}
void FileObject::_Close(bool final)
{
    const auto host = _OwnerHost();
    mm_file_owner_close(&_file, &host, final);
}
void FileObject::Flush()
{
    const auto host = _OwnerHost();
    mm_file_owner_flush(&_file, &host);
}
void FileObject::_SetFpPos(FilePosition position)
{
    const auto host = _OwnerHost();
    mm_file_owner_seek(&_file, &host, position);
}
bool FileObject::FSetFpMac(FilePosition bytes)
{
    const auto host = _OwnerHost();
    return mm_file_owner_resize(&_file, &host, bytes) != 0;
}
FilePosition FileObject::FpMac()
{
    const auto host = _OwnerHost();
    return static_cast<FilePosition>(mm_file_owner_size(&_file, &host));
}
bool FileObject::FReadRgb(void *data, LegacyLong bytes, FilePosition position)
{
    const auto host = _OwnerHost();
    return mm_file_owner_read(&_file, &host, data, bytes, position) != 0;
}
bool FileObject::FWriteRgb(void *data, LegacyLong bytes, FilePosition position)
{
    const auto host = _OwnerHost();
    return mm_file_owner_write(&_file, &host, data, bytes, position) != 0;
}

/***************************************************************************
    Swap the names of the two files.  They should be in the same directory.
***************************************************************************/
bool FileObject::FSwapNames(PFileObject pfil)
{
    AssertThis(0);
    AssertPo(pfil, 0);
    Filename scratch, unused;
    const auto host = _OpsHost();
    return mm_file_ops_swap(&host, this, pfil, &scratch) != 0;
}

/***************************************************************************
    Rename a file.  The new fni should be on the same volume.
    This may fail without an error code being set.
***************************************************************************/
bool FileObject::FRename(Filename *pfni)
{
    AssertThis(0);
    AssertPo(pfni, ffniFile);
    Filename unused;
    const auto host = _OpsHost();
    return mm_file_ops_rename(&host, this, pfni) != 0;
}
