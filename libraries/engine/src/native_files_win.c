/* Native file services converted from Kauai platform_files_win.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/native_files.h"
#include <string.h>
#include <stdlib.h>
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
static const TCHAR *native(const MMNativePathChar *p) { return (const TCHAR *)p; }
static TCHAR *native_out(MMNativePathChar *p) { return (TCHAR *)p; }
static MMNativePathInfo attributes(DWORD bits)
{
    MMNativePathInfo info;
    info.directory = !!(bits & FILE_ATTRIBUTE_DIRECTORY); info.hidden = !!(bits & FILE_ATTRIBUTE_HIDDEN);
    info.system = !!(bits & FILE_ATTRIBUTE_SYSTEM); info.read_only = !!(bits & FILE_ATTRIBUTE_READONLY);
    info.link = !!(bits & FILE_ATTRIBUTE_REPARSE_POINT); return info;
}
static void entry(const WIN32_FIND_DATA *data, MMNativePathChar *name, MMNativePathInfo *info)
{
    memcpy(name,data->cFileName,sizeof(data->cFileName)); *info = attributes(data->dwFileAttributes);
}
void mm_native_open_options_init(MMNativeOpenOptions *o)
{ o->write = o->deny_read = 0; o->deny_write = 1; o->creation = MM_NATIVE_OPEN_EXISTING; }
void *mm_native_file_open(const MMNativePathChar *path, const MMNativeOpenOptions *o)
{
    DWORD access = GENERIC_READ | (o->write ? GENERIC_WRITE : 0);
    DWORD sharing = (o->deny_read ? 0 : FILE_SHARE_READ) | (o->deny_write ? 0 : FILE_SHARE_WRITE);
    DWORD creation = o->creation == MM_NATIVE_OPEN_EXISTING ? OPEN_EXISTING :
                     o->creation == MM_NATIVE_CREATE_ALWAYS ? CREATE_ALWAYS : CREATE_NEW;
    HANDLE handle = CreateFile(native(path),access,sharing,NULL,creation,FILE_ATTRIBUTE_NORMAL,NULL);
    return handle == INVALID_HANDLE_VALUE ? NULL : handle;
}
void mm_native_file_close(void *handle) { if (handle) CloseHandle(handle); }
int mm_native_file_flush(void *handle) { return FlushFileBuffers(handle) != 0; }
int mm_native_file_seek(void *handle, MMU32 position)
{ return position <= 0x7fffffffUL && SetFilePointer(handle,position,NULL,FILE_BEGIN) != INVALID_SET_FILE_POINTER; }
int mm_native_file_resize(void *handle, MMU32 bytes) { return mm_native_file_seek(handle,bytes) && SetEndOfFile(handle); }
int mm_native_file_size(void *handle, MMU32 *bytes)
{
    LARGE_INTEGER length;
    if (!GetFileSizeEx(handle,&length) || length.QuadPart < 0 || length.QuadPart > 0x7fffffffL) return 0;
    *bytes = (MMU32)length.QuadPart; return 1;
}
int mm_native_file_read(void *handle, void *data, MMU32 bytes)
{ DWORD transferred; return ReadFile(handle,data,bytes,&transferred,NULL) && transferred == bytes; }
int mm_native_file_write(void *handle, const void *data, MMU32 bytes)
{ DWORD transferred; return WriteFile(handle,data,bytes,&transferred,NULL) && transferred == bytes; }
int mm_native_path_info(const MMNativePathChar *path, MMNativePathInfo *info)
{
    DWORD bits = GetFileAttributes(native(path)); DWORD error;
    if (bits != INVALID_FILE_ATTRIBUTES) { *info = attributes(bits); return MM_NATIVE_FOUND; }
    error = GetLastError();
    /* A missing parent remains an error for Filename::TExists. */
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_INVALID_DRIVE ? MM_NATIVE_MISSING : MM_NATIVE_ERROR;
}
int mm_native_path_delete(const MMNativePathChar *path) { return DeleteFile(native(path)) != 0; }
int mm_native_path_rename(const MMNativePathChar *source, const MMNativePathChar *destination)
{
    DWORD bits = GetFileAttributes(native(source));
    return bits != INVALID_FILE_ATTRIBUTES && !(bits & FILE_ATTRIBUTE_READONLY) && MoveFile(native(source),native(destination));
}
int mm_native_make_directory(const MMNativePathChar *path) { return CreateDirectory(native(path),NULL) != 0; }
int mm_native_delete_directory(const MMNativePathChar *path) { return RemoveDirectory(native(path)) != 0; }
int mm_native_paths_equal(const MMNativePathChar *a, const MMNativePathChar *b)
{
    size_t count = (size_t)lstrlen(native(a)), block;
    TCHAR left[255], right[255];
    if (count != (size_t)lstrlen(native(b))) return 0;
    while (count) {
        block = count < 255 ? count : 255;
        memcpy(left,native(a),block * sizeof(TCHAR)); memcpy(right,native(b),block * sizeof(TCHAR));
        CharUpperBuff(left,(DWORD)block); CharUpperBuff(right,(DWORD)block);
        if (memcmp(left,right,block * sizeof(TCHAR))) return 0;
        a += block; b += block; count -= block;
    }
    return 1;
}
int mm_native_is_separator(MMNativePathChar ch) { return ch == '\\' || ch == '/'; }
MMNativePathChar mm_native_separator(void) { return '\\'; }
size_t mm_native_lookup_leaf(const MMNativePathChar *path, MMNativePathChar *output, size_t capacity)
{
    WIN32_FIND_DATA data; HANDLE handle; size_t length = 0;
    if (!output || !capacity) return 0;
    output[0] = 0; if (!path || !*path) return 0;
    handle = FindFirstFile(native(path),&data); if (handle == INVALID_HANDLE_VALUE) return 0;
    FindClose(handle); while (data.cFileName[length]) ++length;
    if (!length || length >= capacity) return 0;
    memcpy(output,data.cFileName,(length + 1) * sizeof(MMNativePathChar)); return length;
}
size_t mm_native_full_path(const MMNativePathChar *input, MMNativePathChar *output, size_t capacity, MMNativePathChar **leaf)
{
    TCHAR *native_leaf;
    DWORD length = GetFullPathName(native(input),(DWORD)capacity,native_out(output),&native_leaf);
    if (!length || length >= capacity) return 0;
    if (leaf) *leaf = (MMNativePathChar *)native_leaf;
    return length;
}
size_t mm_native_find_path(const MMNativePathChar *input, const MMNativePathChar *search, MMNativePathChar *output, size_t capacity)
{
    DWORD length = SearchPath(native(search),native(input),NULL,(DWORD)capacity,native_out(output),NULL);
    return !length || length >= capacity ? 0 : length;
}
size_t mm_native_temp_path(MMNativePathChar *output, size_t capacity)
{
    DWORD length = GetTempPath((DWORD)capacity,native_out(output)); return !length || length >= capacity ? 0 : length;
}
int mm_native_make_directories(const MMNativePathChar *path)
{
    MMNativePathChar *copy; size_t length, i; MMNativePathInfo info; int result, absolute;
    if (!path || !*path) return 0;
    absolute = (mm_native_is_separator(path[0]) && mm_native_is_separator(path[1])) ||
               (path[1] == ':' && mm_native_is_separator(path[2]));
    if (!absolute) return 0;
    length = (size_t)lstrlen(native(path));
    if (length >= (size_t)-1 / sizeof(*copy)) return 0;
    copy = (MMNativePathChar *)malloc((length + 1) * sizeof(*copy)); if (!copy) return 0;
    memcpy(copy,path,(length + 1) * sizeof(*copy));
    for (i = 0; i < length; ++i) if (copy[i] == '/') copy[i] = '\\';
    if (mm_native_path_info(copy,&info) == MM_NATIVE_FOUND) { result = info.directory; free(copy); return result; }
    /* Keep the existing ownerless shell call and confirm the directory. */
    SHCreateDirectoryEx(NULL,native(copy),NULL);
    result = mm_native_path_info(copy,&info) == MM_NATIVE_FOUND && info.directory;
    free(copy); return result;
}
int mm_native_change_directory(const MMNativePathChar *path, MMU32 *error)
{
    int valid = path && *path;
    int result = valid && SetCurrentDirectory(native(path)) != 0;
    if (error) *error = result ? 0 : valid ? GetLastError() : ERROR_INVALID_PARAMETER;
    return result;
}
size_t mm_native_current_directory(MMNativePathChar *output, size_t capacity)
{
    DWORD length;
    if (!output || !capacity) return 0;
    output[0] = 0; if (capacity > 0xffffffffUL) return 0;
    length = GetCurrentDirectory((DWORD)capacity,native_out(output));
    if (!length || length >= capacity) { output[0] = 0; return 0; }
    return length;
}
size_t mm_native_executable_path(MMNativePathChar *output, size_t capacity)
{
    DWORD length;
    if (!output || !capacity) return 0;
    output[0] = 0; if (capacity > 0xffffffffUL) return 0;
    length = GetModuleFileName(NULL,native_out(output),(DWORD)capacity);
    if (!length || length >= capacity) { output[0] = 0; return 0; }
    return length;
}
MMU32 mm_native_root_mask(void) { return GetLogicalDrives(); }
MMU32 mm_native_suppress_media_errors(void) { return SetErrorMode(SEM_FAILCRITICALERRORS); }
void mm_native_restore_media_errors(MMU32 token) { SetErrorMode(token); }
int mm_native_root_path(unsigned index, MMNativePathChar *output, size_t capacity)
{
    if (index >= 26 || capacity < 4) return 0;
    output[0] = (MMNativePathChar)('A' + index); output[1] = ':'; output[2] = '\\'; output[3] = 0; return 1;
}
MMU32 mm_native_volume_kinds(const MMNativePathChar *path)
{
    TCHAR root[4];
    if (!path[0] || !path[1] || path[1] != ':' || !mm_native_is_separator(path[2])) return 2;
    root[0] = native(path)[0]; root[1] = ':'; root[2] = '\\'; root[3] = 0;
    switch (GetDriveType(root)) {
    case DRIVE_FIXED: case DRIVE_RAMDISK: return 0;
    case DRIVE_REMOVABLE: return 8 | ((root[0] == 'A' || root[0] == 'a' || root[0] == 'B' || root[0] == 'b') ? 1 : 0);
    case DRIVE_CDROM: return 8 | 4;
    default: return 2;
    }
}
int mm_native_directory_first(const MMNativePathChar *pattern, void **handle, MMNativePathChar *name, MMNativePathInfo *info)
{
    WIN32_FIND_DATA data; HANDLE found = FindFirstFile(native(pattern),&data); DWORD error;
    *handle = found == INVALID_HANDLE_VALUE ? NULL : found;
    if (!*handle) { error = GetLastError(); return error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES ? MM_NATIVE_END : MM_NATIVE_ITERATION_ERROR; }
    entry(&data,name,info); return MM_NATIVE_ENTRY;
}
int mm_native_directory_next(void *handle, MMNativePathChar *name, MMNativePathInfo *info)
{
    WIN32_FIND_DATA data;
    if (!FindNextFile(handle,&data)) return GetLastError() == ERROR_NO_MORE_FILES ? MM_NATIVE_END : MM_NATIVE_ITERATION_ERROR;
    entry(&data,name,info); return MM_NATIVE_ENTRY;
}
void mm_native_directory_close(void *handle) { if (handle) FindClose(handle); }
