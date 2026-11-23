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

const MMNativeNamedFileDiagnostics *FileObject::_FileDiagnostics()
{
    static_assert(ffilWriteEnable == MM_FILE_WRITE && ffilDenyWrite == MM_FILE_DENY_WRITE &&
                  ffilDenyRead == MM_FILE_DENY_READ && kgrffilPerm == MM_FILE_PERMISSIONS &&
                  ffilTemp == MM_FILE_TEMP && ffilMark == MM_FILE_MARK, "native file flag contract");
    static_assert(elNil == MM_FILE_ERROR_NONE && kelWrite == MM_FILE_ERROR_WRITE &&
                  kelRead == MM_FILE_ERROR_READ && kelSeek == MM_FILE_ERROR_SEEK &&
                  kelCritical == MM_FILE_ERROR_CRITICAL, "native file error contract");
    static const MMNativeNamedFileDiagnostics diagnostics = {
    [](void *, int code) {
        if (code == MM_FILE_ISSUE_GENERAL) PushErc(ercFileGeneral);
        else if (code == MM_FILE_ISSUE_PERMISSION) PushErc(ercFilePerm);
        else Warn("Deleting temp file failed");
    },
#ifdef DEBUG
    [](void *c, int base) {
        auto *file = static_cast<PFileObject>(c);
        if (base) { AssertBasePo(file, 0); }
        else { AssertPo(file, 0); }
    },
    [](void *, int operation, const void *data, MMI32 bytes, MMI32 position) {
        if (operation == MM_FILE_ARGUMENT_RESIZE) { AssertIn(position, 0, kcbMax); }
        else {
            AssertIn(bytes, 0, kcbMax);
            AssertIn(position, 0, klwMax);
            AssertPvCb(const_cast<void *>(data), bytes);
        }
    },
    [](void *, int code) {
        if (code == MM_FILE_CHECK_WRITE) { Assert(fFalse, "can't write to read only file"); }
        else if (code == MM_FILE_CHECK_CREATE) { Assert(fFalse, "can't create an open file"); }
        else { Assert(fFalse, "read past EOF"); }
    },
    1
#else
    nullptr, nullptr, nullptr, 0
#endif
    };
    return &diagnostics;
}

MMFileOwnerHost FileObject::_OwnerHost()
{
    return mm_native_named_file_host(&_file);
}

bool FileObject::_FOpen(bool create, LegacyUlong flags)
{
    const auto host = _OwnerHost();
    return mm_file_owner_open(&_file.state, &host, create, flags) != 0;
}
void FileObject::_Close(bool final)
{
    const auto host = _OwnerHost();
    mm_file_owner_close(&_file.state, &host, final);
}
void FileObject::Flush()
{
    const auto host = _OwnerHost();
    mm_file_owner_flush(&_file.state, &host);
}
void FileObject::_SetFpPos(FilePosition position)
{
    const auto host = _OwnerHost();
    mm_file_owner_seek(&_file.state, &host, position);
}
bool FileObject::FSetFpMac(FilePosition bytes)
{
    const auto host = _OwnerHost();
    return mm_file_owner_resize(&_file.state, &host, bytes) != 0;
}
FilePosition FileObject::FpMac()
{
    const auto host = _OwnerHost();
    return static_cast<FilePosition>(mm_file_owner_size(&_file.state, &host));
}
bool FileObject::FReadRgb(void *data, LegacyLong bytes, FilePosition position)
{
    const auto host = _OwnerHost();
    return mm_file_owner_read(&_file.state, &host, data, bytes, position) != 0;
}
bool FileObject::FWriteRgb(void *data, LegacyLong bytes, FilePosition position)
{
    const auto host = _OwnerHost();
    return mm_file_owner_write(&_file.state, &host, data, bytes, position) != 0;
}

/***************************************************************************
    Swap the names of the two files.  They should be in the same directory.
***************************************************************************/
bool FileObject::FSwapNames(PFileObject pfil)
{
    Filename scratch;
    const auto host = CoreFileOps();
    return mm_file_ops_swap(&host, &_registration, &pfil->_registration, &scratch) != 0;
}

/***************************************************************************
    Rename a file.  The new fni should be on the same volume.
    This may fail without an error code being set.
***************************************************************************/
bool FileObject::FRename(Filename *pfni)
{
    const auto host = CoreFileOps();
    return mm_file_ops_rename(&host, &_registration, pfni) != 0;
}
