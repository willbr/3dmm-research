/* Native file services converted from Kauai platform_files_posix.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mm/native_files.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef UNICODE
#error POSIX native paths require byte strings
#endif

typedef struct NativeDirectory { DIR *stream; char *path; } NativeDirectory;
static int descriptor(void *handle) { return (int)((ptrdiff_t)handle - 1); }
static void *file_handle(int fd) { return (void *)((ptrdiff_t)fd + 1); }
static int lock_for_mutation(const char *path)
{
    int fd, result, flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    do { fd = open(path, flags); } while (fd < 0 && errno == EINTR);
    if (fd < 0) return -1;
    do { result = flock(fd, LOCK_EX | LOCK_NB); } while (result < 0 && errno == EINTR);
    if (result < 0) { close(fd); return -1; }
    return fd;
}
static size_t copy_path(const char *path, char *output, size_t capacity)
{
    size_t length = strlen(path);
    if (!length || length >= capacity) return 0;
    memcpy(output, path, length + 1);
    return length;
}
static char *join(const char *a, size_t na, const char *b, size_t nb)
{
    char *result;
    if (nb == (size_t)-1 || na > (size_t)-1 - nb - 1) return NULL;
    result = (char *)malloc(na + nb + 1);
    if (result) { memcpy(result, a, na); memcpy(result + na, b, nb); result[na + nb] = 0; }
    return result;
}
static char *normalize(const char *path)
{
    char *input, *cwd, *grown, *result;
    size_t capacity = 256, length, start, end, used, count;
    if (!path || !*path) return NULL;
    if (*path == '/') input = join(path, strlen(path), "", 0);
    else
    {
        cwd = (char *)malloc(capacity);
        if (!cwd) return NULL;
        while (!getcwd(cwd, capacity))
        {
            if (errno != ERANGE || capacity > (size_t)-1 / 2) { free(cwd); return NULL; }
            capacity *= 2;
            grown = (char *)realloc(cwd, capacity);
            if (!grown) { free(cwd); return NULL; }
            cwd = grown;
        }
        length = strlen(cwd);
        input = join(cwd, length, "/", 1);
        free(cwd);
        if (!input) return NULL;
        cwd = join(input, length + 1, path, strlen(path));
        free(input);
        input = cwd;
    }
    if (!input) return NULL;
    length = strlen(input);
    result = (char *)malloc(length + 1);
    if (!result) { free(input); return NULL; }
    /* Lexical normalization: do not follow symlinks or require existence. */
    used = 1; result[0] = '/'; start = 1;
    while (start <= length)
    {
        end = start;
        while (end < length && input[end] != '/') ++end;
        count = end - start;
        if (count == 2 && input[start] == '.' && input[start + 1] == '.')
        {
            while (used > 1 && result[used - 1] != '/') --used;
            if (used > 1) --used;
        }
        else if (count && !(count == 1 && input[start] == '.'))
        {
            if (used > 1) result[used++] = '/';
            memcpy(result + used, input + start, count); used += count;
        }
        start = end + 1;
    }
    if (input[length - 1] == '/' && result[used - 1] != '/') result[used++] = '/';
    result[used] = 0;
    free(input);
    return result;
}
void mm_native_open_options_init(MMNativeOpenOptions *o)
{ o->write = o->deny_read = 0; o->deny_write = 1; o->creation = MM_NATIVE_OPEN_EXISTING; }
void *mm_native_file_open(const char *path, const MMNativeOpenOptions *o)
{
    int fd, result, flags = o->write ? O_RDWR : O_RDONLY;
    if (o->creation != MM_NATIVE_OPEN_EXISTING) flags |= O_CREAT;
    if (o->creation == MM_NATIVE_CREATE_EXCLUSIVE) flags |= O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    do { fd = open(path, flags, 0666); } while (fd < 0 && errno == EINTR);
    if (fd < 0) return NULL;
    /* Acquire the cooperative lock before truncating an existing file. */
    do { result = flock(fd, (o->write || o->deny_read ? LOCK_EX : LOCK_SH) | LOCK_NB); }
    while (result < 0 && errno == EINTR);
    if (result < 0 || (o->creation == MM_NATIVE_CREATE_ALWAYS && ftruncate(fd, 0) != 0))
    { close(fd); return NULL; }
    return file_handle(fd);
}
void *mm_native_file_open_utf8(const char *path, const MMNativeOpenOptions *o)
{
    return path && *path ? mm_native_file_open(path, o) : NULL;
}
void *mm_native_file_temporary(void)
{
    const char suffix[] = "/3mm-XXXXXX";
    const char *configured = getenv("TMPDIR");
    char *directory = normalize(configured && *configured ? configured : "/tmp");
    char *path;
    size_t length, skip;
    int fd, removed;
    if (!directory)
        return NULL;
    length = strlen(directory);
    skip = directory[length - 1] == '/';
    path = join(directory, length, suffix + skip, sizeof(suffix) - 1 - skip);
    free(directory);
    if (!path)
        return NULL;
    fd = mkstemp(path);
    if (fd >= 0)
    {
        do { removed = unlink(path); } while (removed < 0 && errno == EINTR);
        if (removed != 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        {
            close(fd);
            fd = -1;
        }
    }
    free(path);
    return fd < 0 ? NULL : file_handle(fd);
}
void mm_native_file_close(void *h) { if (h) close(descriptor(h)); }
int mm_native_file_flush(void *h)
{ int result; do { result = fsync(descriptor(h)); } while (result < 0 && errno == EINTR); return result == 0; }
int mm_native_file_seek(void *h, MMU32 n)
{ return n <= 2147483647UL && lseek(descriptor(h), (off_t)n, SEEK_SET) == (off_t)n; }
int mm_native_file_resize(void *h, MMU32 n)
{ return n <= 2147483647UL && ftruncate(descriptor(h), n) == 0; }
int mm_native_file_size(void *h, MMU32 *n)
{
    struct stat status;
    if (fstat(descriptor(h), &status) || status.st_size < 0 || status.st_size > 2147483647L) return 0;
    *n = (MMU32)status.st_size; return 1;
}
int mm_native_file_read(void *h, void *data, MMU32 bytes)
{
    unsigned char *output = (unsigned char *)data;
    ssize_t count;
    while (bytes)
    {
        count = read(descriptor(h), output, bytes);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        output += count; bytes -= (MMU32)count;
    }
    return 1;
}
int mm_native_file_write(void *h, const void *data, MMU32 bytes)
{
    const unsigned char *input = (const unsigned char *)data;
    ssize_t count;
    while (bytes)
    {
        count = write(descriptor(h), input, bytes);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        input += count; bytes -= (MMU32)count;
    }
    return 1;
}
int mm_native_path_info(const char *path, MMNativePathInfo *info)
{
    struct stat status, target;
    const char *leaf;
    if (lstat(path, &status) != 0) return errno == ENOENT || errno == ENOTDIR ? MM_NATIVE_MISSING : MM_NATIVE_ERROR;
    memset(info, 0, sizeof(*info)); info->link = !!S_ISLNK(status.st_mode);
    if (info->link)
    {
        if (stat(path, &target) == 0) status = target;
        else if (errno != ENOENT && errno != ENOTDIR) return MM_NATIVE_ERROR;
    }
    info->directory = !!S_ISDIR(status.st_mode);
    info->read_only = (status.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0;
    leaf = strrchr(path, '/'); leaf = leaf ? leaf + 1 : path;
    info->hidden = leaf[0] == '.' && strcmp(leaf, ".") && strcmp(leaf, "..");
    return MM_NATIVE_FOUND;
}
int mm_native_path_delete(const char *path)
{
    MMNativePathInfo info;
    int guard, result;
    if (mm_native_path_info(path, &info) != MM_NATIVE_FOUND || info.directory) return 0;
    if (info.link) return unlink(path) == 0;
    if (info.read_only) return 0;
    guard = lock_for_mutation(path);
    if (guard < 0) return 0;
    result = unlink(path) == 0; close(guard); return result;
}
int mm_native_path_rename(const char *source, const char *destination)
{
    MMNativePathInfo info;
    int guard, result;
    if (mm_native_path_info(source, &info) != MM_NATIVE_FOUND || info.directory || (!info.link && info.read_only)) return 0;
    guard = info.link ? -1 : lock_for_mutation(source);
    if (!info.link && guard < 0) return 0;
    result = linkat(AT_FDCWD, source, AT_FDCWD, destination, 0) == 0;
    if (result && unlink(source) != 0) { unlink(destination); result = 0; }
    if (guard >= 0) close(guard);
    return result;
}
int mm_native_make_directory(const char *p) { return mkdir(p, 0777) == 0; }
int mm_native_delete_directory(const char *p) { return rmdir(p) == 0; }
int mm_native_paths_equal(const char *a, const char *b) { return strcmp(a, b) == 0; }
int mm_native_is_separator(char c) { return c == '/'; }
char mm_native_separator(void) { return '/'; }
int mm_native_make_directories(const char *path)
{
    char *part, saved;
    size_t end, length;
    struct stat info;
    int ok = 1;
    if (!path || path[0] != '/') return 0;
    length = strlen(path); part = join(path, length, "", 0);
    if (!part) return 0;
    for (end = 1; end <= length; ++end)
    {
        if (end < length && part[end] != '/') continue;
        saved = part[end]; part[end] = 0;
        if ((mkdir(part, 0700) != 0 && errno != EEXIST) || stat(part, &info) != 0 || !S_ISDIR(info.st_mode))
        { ok = 0; break; }
        part[end] = saved;
    }
    free(part); return ok;
}
int mm_native_change_directory(const char *path, MMU32 *error)
{
    int valid = path && *path;
    int result = valid && chdir(path) == 0;
    if (error) *error = result ? 0 : valid ? (MMU32)errno : (MMU32)EINVAL;
    return result;
}
size_t mm_native_current_directory(char *output, size_t capacity)
{
    if (!output || !capacity) return 0;
    output[0] = 0;
    if (!getcwd(output, capacity)) { output[0] = 0; return 0; }
    return strlen(output);
}
size_t mm_native_executable_path(char *output, size_t capacity)
{
#ifdef __APPLE__
    uint32_t length;
    size_t result;
#elif defined(__linux__)
    ssize_t length;
#endif
    if (!output || !capacity) return 0;
    output[0] = 0;
#ifdef __APPLE__
    if (capacity > 0xffffffffUL) return 0;
    length = (uint32_t)capacity;
    if (_NSGetExecutablePath(output, &length) != 0) { output[0] = 0; return 0; }
    result = mm_native_full_path(output, output, capacity, NULL);
    if (!result) output[0] = 0;
    return result;
#elif defined(__linux__)
    length = readlink("/proc/self/exe", output, capacity);
    if (length < 0 || (size_t)length >= capacity) { output[0] = 0; return 0; }
    output[length] = 0; return (size_t)length;
#else
    return 0;
#endif
}
size_t mm_native_lookup_leaf(const char *path, char *output, size_t capacity)
{
    struct stat info;
    const char *separator, *leaf;
    if (!output || !capacity) return 0;
    output[0] = 0;
    if (!path || !*path || lstat(path, &info) != 0) return 0;
    separator = strrchr(path, '/'); leaf = separator ? separator + 1 : path;
    return copy_path(leaf, output, capacity);
}
size_t mm_native_full_path(const char *input, char *output, size_t capacity, char **leaf)
{
    char *path = normalize(input);
    size_t length;
    if (!path) return 0;
    length = copy_path(path, output, capacity);
    if (length && leaf) *leaf = path[length - 1] == '/' ? NULL : output + (strrchr(path, '/') - path) + 1;
    free(path); return length;
}
size_t mm_native_find_path(const char *input, const char *search, char *output, size_t capacity)
{
    MMNativePathInfo info;
    const char *end;
    char *directory, *candidate;
    size_t length, result;
    if (strchr(input, '/'))
        return mm_native_path_info(input, &info) == MM_NATIVE_FOUND && !info.directory ? mm_native_full_path(input, output, capacity, NULL) : 0;
    if (!search) search = getenv("PATH");
    if (!search) search = "";
    do
    {
        end = strchr(search, ':'); length = end ? (size_t)(end - search) : strlen(search);
        directory = length ? join(search, length, "/", 1) : join("./", 2, "", 0);
        if (!directory) return 0;
        candidate = join(directory, strlen(directory), input, strlen(input)); free(directory);
        if (!candidate) return 0;
        if (mm_native_path_info(candidate, &info) == MM_NATIVE_FOUND && !info.directory)
        { result = mm_native_full_path(candidate, output, capacity, NULL); free(candidate); return result; }
        free(candidate);
        if (!end) break;
        search = end + 1;
    } while (1);
    return 0;
}
size_t mm_native_temp_path(char *output, size_t capacity)
{
    const char *configured = getenv("TMPDIR");
    char *path = normalize(configured && *configured ? configured : "/tmp");
    char *extended;
    size_t length, result;
    if (!path) return 0;
    length = strlen(path);
    if (path[length - 1] != '/')
    {
        extended = join(path, length, "/", 1); free(path); path = extended;
        if (!path) return 0;
    }
    result = copy_path(path, output, capacity); free(path); return result;
}
MMU32 mm_native_root_mask(void) { return 1; }
MMU32 mm_native_suppress_media_errors(void) { return 0; }
void mm_native_restore_media_errors(MMU32 token) { (void)token; }
int mm_native_root_path(unsigned index, char *output, size_t capacity)
{ return index == 0 && copy_path("/", output, capacity) != 0; }
MMU32 mm_native_volume_kinds(const char *path) { (void)path; return 0; }
int mm_native_directory_first(const char *pattern, void **handle, char *name, MMNativePathInfo *info)
{
    NativeDirectory *directory;
    size_t length;
    *handle = NULL;
    length = strlen(pattern);
    if (!length || pattern[length - 1] != '*') return MM_NATIVE_ITERATION_ERROR;
    directory = (NativeDirectory *)malloc(sizeof(*directory));
    if (!directory) return MM_NATIVE_ITERATION_ERROR;
    directory->path = join(pattern, length - 1, "", 0);
    if (!directory->path) { free(directory); return MM_NATIVE_ITERATION_ERROR; }
    directory->stream = opendir(directory->path);
    if (!directory->stream) { free(directory->path); free(directory); return MM_NATIVE_ITERATION_ERROR; }
    *handle = directory; return mm_native_directory_next(directory, name, info);
}
int mm_native_directory_next(void *handle, char *name, MMNativePathInfo *info)
{
    NativeDirectory *directory = (NativeDirectory *)handle;
    struct dirent *item;
    char *path;
    int result;
    if (!directory) return MM_NATIVE_ITERATION_ERROR;
    errno = 0; item = readdir(directory->stream);
    if (!item) return errno ? MM_NATIVE_ITERATION_ERROR : MM_NATIVE_END;
    if (!copy_path(item->d_name, name, 1024)) return MM_NATIVE_ITERATION_ERROR;
    path = join(directory->path, strlen(directory->path), item->d_name, strlen(item->d_name));
    if (!path) return MM_NATIVE_ITERATION_ERROR;
    result = mm_native_path_info(path, info); free(path);
    return result == MM_NATIVE_FOUND ? MM_NATIVE_ENTRY : MM_NATIVE_ITERATION_ERROR;
}
void mm_native_directory_close(void *handle)
{
    NativeDirectory *directory = (NativeDirectory *)handle;
    if (directory) { closedir(directory->stream); free(directory->path); free(directory); }
}
