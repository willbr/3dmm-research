/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "platform_files.h"
#include <cstring>
#include <limits>
#include <string>
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

namespace
{
using namespace KauaiFiles;
#ifdef UNICODE
using NativeChar = wchar_t;
#else
using NativeChar = char;
#endif
const NativeChar *Native(const PathChar *path)
{
    return reinterpret_cast<const NativeChar *>(path);
}
NativeChar *Native(PathChar *path)
{
    return reinterpret_cast<NativeChar *>(path);
}
PathInfo Attributes(DWORD attributes)
{
    PathInfo info;
    info.directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info.hidden = (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    info.system = (attributes & FILE_ATTRIBUTE_SYSTEM) != 0;
    info.readOnly = (attributes & FILE_ATTRIBUTE_READONLY) != 0;
    info.link = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    return info;
}
void Entry(const WIN32_FIND_DATA &native, DirectoryEntry *entry)
{
    static_assert(sizeof(native.cFileName) <= sizeof(entry->name), "directory name buffer too small");
    std::memcpy(entry->name, native.cFileName, sizeof(native.cFileName));
    entry->info = Attributes(native.dwFileAttributes);
}
} // namespace

KauaiFiles::FileHandle KauaiFiles::Open(const PathChar *path, const OpenOptions &options)
{
    const DWORD access = GENERIC_READ | (options.write ? GENERIC_WRITE : 0);
    const DWORD sharing = (options.denyRead ? 0 : FILE_SHARE_READ) | (options.denyWrite ? 0 : FILE_SHARE_WRITE);
    const DWORD creation = options.creation == Creation::OpenExisting   ? OPEN_EXISTING
                           : options.creation == Creation::CreateAlways ? CREATE_ALWAYS
                                                                        : CREATE_NEW;
    HANDLE handle = CreateFile(Native(path), access, sharing, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}
void KauaiFiles::Close(FileHandle handle)
{
    if (handle)
        CloseHandle(handle);
}
bool KauaiFiles::Flush(FileHandle handle)
{
    return FlushFileBuffers(handle) != 0;
}
bool KauaiFiles::Seek(FileHandle handle, std::uint32_t position)
{
    return position <= INT32_MAX && SetFilePointer(handle, position, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
}
bool KauaiFiles::Resize(FileHandle handle, std::uint32_t bytes)
{
    return Seek(handle, bytes) && SetEndOfFile(handle);
}
bool KauaiFiles::Size(FileHandle handle, std::uint32_t *bytes)
{
    LARGE_INTEGER length;
    if (!GetFileSizeEx(handle, &length) || length.QuadPart < 0 || length.QuadPart > INT32_MAX)
        return false;
    *bytes = static_cast<std::uint32_t>(length.QuadPart);
    return true;
}
bool KauaiFiles::Read(FileHandle handle, void *data, std::uint32_t bytes)
{
    DWORD transferred;
    return ReadFile(handle, data, bytes, &transferred, nullptr) && transferred == bytes;
}
bool KauaiFiles::Write(FileHandle handle, const void *data, std::uint32_t bytes)
{
    DWORD transferred;
    return WriteFile(handle, data, bytes, &transferred, nullptr) && transferred == bytes;
}
KauaiFiles::Status KauaiFiles::Info(const PathChar *path, PathInfo *info)
{
    DWORD attributes = GetFileAttributes(Native(path));
    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        *info = Attributes(attributes);
        return Status::Found;
    }
    DWORD error = GetLastError();
    // Preserve Filename::TExists' distinction for a missing parent directory.
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_INVALID_DRIVE ? Status::Missing : Status::Error;
}
bool KauaiFiles::Delete(const PathChar *path)
{
    return DeleteFile(Native(path)) != 0;
}
bool KauaiFiles::Rename(const PathChar *source, const PathChar *destination)
{
    DWORD attributes = GetFileAttributes(Native(source));
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_READONLY) &&
           MoveFile(Native(source), Native(destination));
}
bool KauaiFiles::MakeDirectory(const PathChar *path)
{
    return ::CreateDirectory(Native(path), nullptr) != 0;
}
bool KauaiFiles::DeleteDirectory(const PathChar *path)
{
    return ::RemoveDirectory(Native(path)) != 0;
}
bool KauaiFiles::EqualPaths(const PathChar *a, const PathChar *b)
{
    // Match legacy String::FEqualUser, which uppercases equal-length blocks.
    // Locale collation (CompareString) can equate different stored spellings.
    std::size_t count = lstrlen(Native(a));
    if (count != static_cast<std::size_t>(lstrlen(Native(b))))
        return false;
    NativeChar left[255], right[255];
    while (count)
    {
        const auto block = count < 255 ? count : 255;
        std::memcpy(left, Native(a), block * sizeof(NativeChar));
        std::memcpy(right, Native(b), block * sizeof(NativeChar));
        CharUpperBuff(left, static_cast<DWORD>(block));
        CharUpperBuff(right, static_cast<DWORD>(block));
        if (std::memcmp(left, right, block * sizeof(NativeChar)))
            return false;
        a += block;
        b += block;
        count -= block;
    }
    return true;
}
bool KauaiFiles::IsSeparator(PathChar ch)
{
    return ch == '\\' || ch == '/';
}
KauaiFiles::PathChar KauaiFiles::Separator()
{
    return '\\';
}
std::size_t KauaiFiles::LookupLeafName(const PathChar *path, PathChar *output, std::size_t capacity)
{
    if (!output || !capacity)
        return 0;
    output[0] = 0;
    if (!path || !*path)
        return 0;
    WIN32_FIND_DATA data;
    const auto handle = FindFirstFile(Native(path), &data);
    if (handle == INVALID_HANDLE_VALUE)
        return 0;
    FindClose(handle);
    std::size_t length = 0;
    while (data.cFileName[length])
        ++length;
    if (!length || length >= capacity)
        return 0;
    std::memcpy(output, data.cFileName, (length + 1) * sizeof(PathChar));
    return length;
}
std::size_t KauaiFiles::FullPath(const PathChar *input, PathChar *output, std::size_t capacity, PathChar **leaf)
{
    NativeChar *nativeLeaf;
    DWORD length = GetFullPathName(Native(input), static_cast<DWORD>(capacity), Native(output), &nativeLeaf);
    if (length == 0 || length >= capacity)
        return 0;
    if (leaf)
        *leaf = reinterpret_cast<PathChar *>(nativeLeaf);
    return length;
}
std::size_t KauaiFiles::FindInPath(const PathChar *input, const PathChar *search, PathChar *output,
                                   std::size_t capacity)
{
    DWORD length =
        ::SearchPath(Native(search), Native(input), nullptr, static_cast<DWORD>(capacity), Native(output), nullptr);
    return length == 0 || length >= capacity ? 0 : length;
}
std::size_t KauaiFiles::TempPath(PathChar *output, std::size_t capacity)
{
    DWORD length = GetTempPath(static_cast<DWORD>(capacity), Native(output));
    return length == 0 || length >= capacity ? 0 : length;
}
bool KauaiFiles::MakeDirectories(const PathChar *path)
{
    if (!path || !*path)
        return false;
    const bool absolute = (IsSeparator(path[0]) && IsSeparator(path[1])) || (path[1] == ':' && IsSeparator(path[2]));
    if (!absolute)
        return false;
    try
    {
        std::basic_string<PathChar> nativePath(path);
        for (auto &ch : nativePath)
            if (ch == '/')
                ch = '\\';
        PathInfo info;
        if (Info(nativePath.c_str(), &info) == Status::Found)
            return info.directory;
        // Shell directory creation requires backslashes. A null owner guarantees
        // no UI; confirm the resulting directory even if shell visibility checks
        // return a warning, matching Studio's existing fallback behavior.
        SHCreateDirectoryEx(nullptr, Native(nativePath.c_str()), nullptr);
        return Info(nativePath.c_str(), &info) == Status::Found && info.directory;
    }
    catch (...)
    {
        return false;
    }
}
bool KauaiFiles::ChangeDirectory(const PathChar *path, std::uint32_t *error)
{
    const bool valid = path && *path;
    const bool result = valid && SetCurrentDirectory(Native(path)) != 0;
    if (error)
        *error = result ? 0 : valid ? GetLastError() : ERROR_INVALID_PARAMETER;
    return result;
}
std::size_t KauaiFiles::CurrentDirectory(PathChar *output, std::size_t capacity)
{
    if (!output || !capacity)
        return 0;
    output[0] = 0;
    if (capacity > UINT32_MAX)
        return 0;
    const auto length = GetCurrentDirectory(static_cast<DWORD>(capacity), Native(output));
    if (!length || length >= capacity)
    {
        output[0] = 0;
        return 0;
    }
    return length;
}
std::size_t KauaiFiles::ExecutablePath(PathChar *output, std::size_t capacity)
{
    if (!output || !capacity)
        return 0;
    output[0] = 0;
    if (capacity > UINT32_MAX)
        return 0;
    const auto length = GetModuleFileName(nullptr, Native(output), static_cast<DWORD>(capacity));
    if (!length || length >= capacity)
    {
        output[0] = 0;
        return 0;
    }
    return length;
}
std::uint32_t KauaiFiles::RootMask()
{
    return GetLogicalDrives();
}
std::uint32_t KauaiFiles::SuppressMediaErrors()
{
    return SetErrorMode(SEM_FAILCRITICALERRORS);
}
void KauaiFiles::RestoreMediaErrors(std::uint32_t token)
{
    SetErrorMode(token);
}
bool KauaiFiles::RootPath(unsigned index, PathChar *output, std::size_t capacity)
{
    if (index >= 26 || capacity < 4)
        return false;
    output[0] = static_cast<PathChar>('A' + index);
    output[1] = ':';
    output[2] = '\\';
    output[3] = 0;
    return true;
}
std::uint32_t KauaiFiles::VolumeKinds(const PathChar *path)
{
    if (!path[0] || !path[1] || path[1] != ':' || !IsSeparator(path[2]))
        return 2;
    NativeChar root[] = {Native(path)[0], ':', '\\', 0};
    switch (GetDriveType(root))
    {
    case DRIVE_FIXED:
    case DRIVE_RAMDISK:
        return 0;
    case DRIVE_REMOVABLE:
        return 8 | ((root[0] == 'A' || root[0] == 'a' || root[0] == 'B' || root[0] == 'b') ? 1 : 0);
    case DRIVE_CDROM:
        return 8 | 4;
    default:
        return 2;
    }
}
KauaiFiles::Iteration KauaiFiles::First(const PathChar *pattern, DirectoryHandle *handle, DirectoryEntry *entry)
{
    WIN32_FIND_DATA native;
    HANDLE found = FindFirstFile(Native(pattern), &native);
    *handle = found == INVALID_HANDLE_VALUE ? nullptr : found;
    if (!*handle)
    {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES ? Iteration::End : Iteration::Error;
    }
    Entry(native, entry);
    return Iteration::Entry;
}
KauaiFiles::Iteration KauaiFiles::Next(DirectoryHandle handle, DirectoryEntry *entry)
{
    WIN32_FIND_DATA native;
    if (!FindNextFile(handle, &native))
        return GetLastError() == ERROR_NO_MORE_FILES ? Iteration::End : Iteration::Error;
    Entry(native, entry);
    return Iteration::Entry;
}
void KauaiFiles::CloseDirectory(DirectoryHandle handle)
{
    if (handle)
        FindClose(handle);
}
