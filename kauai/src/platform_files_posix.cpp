/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "platform_files.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <new>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static_assert(sizeof(KauaiFiles::PathChar) == 1, "POSIX paths require byte strings");
namespace
{
using namespace KauaiFiles;
int Descriptor(FileHandle handle)
{
    return static_cast<int>(reinterpret_cast<std::intptr_t>(handle) - 1);
}
FileHandle Handle(int fd)
{
    return reinterpret_cast<FileHandle>(static_cast<std::intptr_t>(fd) + 1);
}
int LockForMutation(const char *path)
{
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int fd;
    do
    {
        fd = open(path, flags);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return -1;
    int result;
    do
    {
        result = flock(fd, LOCK_EX | LOCK_NB);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}
struct Directory
{
    DIR *stream;
    std::string path;
};
std::size_t Copy(const std::string &path, char *output, std::size_t capacity)
{
    if (path.empty() || path.size() >= capacity)
        return 0;
    std::memcpy(output, path.c_str(), path.size() + 1);
    return path.size();
}
std::string Normalize(const char *path)
{
    if (!path || !*path)
        return {};
    std::string input(path);
    if (input[0] != '/')
    {
        std::vector<char> cwd(256);
        while (!getcwd(cwd.data(), cwd.size()))
        {
            if (errno != ERANGE)
                return {};
            cwd.resize(cwd.size() * 2);
        }
        input = std::string(cwd.data()) + '/' + input;
    }
    // Lexical normalization also works for names which do not exist yet.
    // Symlinks are deliberately not resolved, matching the Windows wrapper.
    std::vector<std::string> parts;
    std::size_t start = 1;
    while (start <= input.size())
    {
        auto end = input.find('/', start);
        if (end == std::string::npos)
            end = input.size();
        auto part = input.substr(start, end - start);
        if (part == "..")
        {
            if (!parts.empty())
                parts.pop_back();
        }
        else if (!part.empty() && part != ".")
            parts.push_back(part);
        start = end + 1;
    }
    std::string result = "/";
    for (const auto &part : parts)
    {
        if (result.size() > 1)
            result += '/';
        result += part;
    }
    if (input.back() == '/' && result.back() != '/')
        result += '/';
    return result;
}
} // namespace

KauaiFiles::FileHandle KauaiFiles::Open(const PathChar *path, const OpenOptions &options)
{
    int flags = options.write ? O_RDWR : O_RDONLY;
    if (options.creation != Creation::OpenExisting)
        flags |= O_CREAT;
    if (options.creation == Creation::CreateExclusive)
        flags |= O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int fd;
    do
    {
        fd = open(path, flags, 0666);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return nullptr;
    // POSIX has no Windows share-mode equivalent. Cooperative readers take a
    // shared advisory lock; writers/deny-read users take an exclusive one.
    // This is conservative: concurrent reader/writer sharing is not supported.
    // Never truncate an existing file before acquiring its lock.
    int result;
    do
    {
        result = flock(fd, (options.write || options.denyRead ? LOCK_EX : LOCK_SH) | LOCK_NB);
    } while (result < 0 && errno == EINTR);
    if (result < 0 || (options.creation == Creation::CreateAlways && ftruncate(fd, 0) != 0))
    {
        close(fd);
        return nullptr;
    }
    return Handle(fd);
}
void KauaiFiles::Close(FileHandle handle)
{
    if (handle)
        close(Descriptor(handle));
}
bool KauaiFiles::Flush(FileHandle handle)
{
    int result;
    do
    {
        result = fsync(Descriptor(handle));
    } while (result < 0 && errno == EINTR);
    return result == 0;
}
bool KauaiFiles::Seek(FileHandle handle, std::uint32_t position)
{
    return position <= INT32_MAX && lseek(Descriptor(handle), position, SEEK_SET) == position;
}
bool KauaiFiles::Resize(FileHandle handle, std::uint32_t bytes)
{
    return bytes <= INT32_MAX && ftruncate(Descriptor(handle), bytes) == 0;
}
bool KauaiFiles::Size(FileHandle handle, std::uint32_t *bytes)
{
    struct stat status;
    if (fstat(Descriptor(handle), &status) || status.st_size < 0 || status.st_size > INT32_MAX)
        return false;
    *bytes = static_cast<std::uint32_t>(status.st_size);
    return true;
}
bool KauaiFiles::Read(FileHandle handle, void *data, std::uint32_t bytes)
{
    auto *output = static_cast<unsigned char *>(data);
    while (bytes)
    {
        ssize_t count = read(Descriptor(handle), output, bytes);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        output += count;
        bytes -= static_cast<std::uint32_t>(count);
    }
    return true;
}
bool KauaiFiles::Write(FileHandle handle, const void *data, std::uint32_t bytes)
{
    auto *input = static_cast<const unsigned char *>(data);
    while (bytes)
    {
        ssize_t count = write(Descriptor(handle), input, bytes);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        input += count;
        bytes -= static_cast<std::uint32_t>(count);
    }
    return true;
}
KauaiFiles::Status KauaiFiles::Info(const PathChar *path, PathInfo *info)
{
    struct stat status;
    if (lstat(path, &status) != 0)
        return errno == ENOENT || errno == ENOTDIR ? Status::Missing : Status::Error;
    *info = PathInfo{};
    info->link = S_ISLNK(status.st_mode);
    if (info->link)
    {
        struct stat target;
        if (stat(path, &target) == 0)
            status = target;
        else if (errno != ENOENT && errno != ENOTDIR)
            return Status::Error;
    }
    info->directory = S_ISDIR(status.st_mode);
    info->readOnly = (status.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0;
    const char *leaf = std::strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    info->hidden = leaf[0] == '.' && std::strcmp(leaf, ".") && std::strcmp(leaf, "..");
    return Status::Found;
}
bool KauaiFiles::Delete(const PathChar *path)
{
    PathInfo info;
    if (Info(path, &info) != Status::Found || info.directory)
        return false;
    if (info.link)
        return unlink(path) == 0; // Delete the link, including a dangling link.
    if (info.readOnly)
        return false;
    int guard = LockForMutation(path);
    if (guard < 0)
        return false;
    bool result = unlink(path) == 0;
    close(guard);
    return result;
}
bool KauaiFiles::Rename(const PathChar *source, const PathChar *destination)
{
    PathInfo info;
    if (Info(source, &info) != Status::Found || info.directory || (!info.link && info.readOnly))
        return false;
    // Atomic no-clobber destination creation on the same filesystem. Unlike
    // rename(), link() cannot silently replace an existing destination.
    int guard = info.link ? -1 : LockForMutation(source);
    if (!info.link && guard < 0)
        return false;
    bool result = linkat(AT_FDCWD, source, AT_FDCWD, destination, 0) == 0;
    if (result && unlink(source) != 0)
    {
        unlink(destination);
        result = false;
    }
    if (guard >= 0)
        close(guard);
    return result;
}
bool KauaiFiles::MakeDirectory(const PathChar *path)
{
    return mkdir(path, 0777) == 0;
}
bool KauaiFiles::DeleteDirectory(const PathChar *path)
{
    return rmdir(path) == 0;
}
bool KauaiFiles::EqualPaths(const PathChar *a, const PathChar *b)
{
    return std::strcmp(a, b) == 0;
}
bool KauaiFiles::IsSeparator(PathChar ch)
{
    return ch == '/';
}
KauaiFiles::PathChar KauaiFiles::Separator()
{
    return '/';
}
bool KauaiFiles::MakeDirectories(const PathChar *path)
{
    if (!path || path[0] != '/')
        return false;
    try
    {
        const std::string full(path);
        for (std::size_t end = 1; end <= full.length(); ++end)
        {
            if (end < full.length() && full[end] != '/')
                continue;
            const auto part = full.substr(0, end);
            if (mkdir(part.c_str(), 0700) != 0 && errno != EEXIST)
                return false;
            struct stat info;
            if (stat(part.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
                return false;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}
bool KauaiFiles::ChangeDirectory(const PathChar *path, std::uint32_t *error)
{
    const bool valid = path && *path;
    const bool result = valid && chdir(path) == 0;
    if (error)
        *error = result ? 0 : valid ? errno : EINVAL;
    return result;
}
std::size_t KauaiFiles::CurrentDirectory(PathChar *output, std::size_t capacity)
{
    if (!output || !capacity)
        return 0;
    output[0] = 0;
    if (!getcwd(output, capacity))
    {
        output[0] = 0;
        return 0;
    }
    return std::strlen(output);
}
std::size_t KauaiFiles::ExecutablePath(PathChar *output, std::size_t capacity)
{
    if (!output || !capacity)
        return 0;
    output[0] = 0;
#ifdef __APPLE__
    if (capacity > UINT32_MAX)
        return 0;
    auto length = static_cast<std::uint32_t>(capacity);
    if (_NSGetExecutablePath(output, &length) != 0)
    {
        output[0] = 0;
        return 0;
    }
    const auto result = FullPath(output, output, capacity);
    if (!result)
        output[0] = 0;
    return result;
#elif defined(__linux__)
    const auto length = readlink("/proc/self/exe", output, capacity);
    if (length < 0 || static_cast<std::size_t>(length) >= capacity)
    {
        output[0] = 0;
        return 0;
    }
    output[length] = 0;
    return static_cast<std::size_t>(length);
#else
    return 0;
#endif
}
std::size_t KauaiFiles::LookupLeafName(const PathChar *path, PathChar *output, std::size_t capacity)
{
    if (!output || !capacity)
        return 0;
    output[0] = 0;
    if (!path || !*path)
        return 0;
    struct stat info;
    if (lstat(path, &info) != 0)
        return 0;
    const auto separator = std::strrchr(path, '/');
    const auto leaf = separator ? separator + 1 : path;
    const auto length = std::strlen(leaf);
    if (!length || length >= capacity)
        return 0;
    std::memcpy(output, leaf, length + 1);
    return length;
}
std::size_t KauaiFiles::FullPath(const PathChar *input, PathChar *output, std::size_t capacity, PathChar **leaf)
{
    try
    {
        auto path = Normalize(input);
        auto length = Copy(path, output, capacity);
        if (length && leaf)
            *leaf = path.back() == '/' ? nullptr : output + path.find_last_of('/') + 1;
        return length;
    }
    catch (...)
    {
        return 0;
    }
}
std::size_t KauaiFiles::FindInPath(const PathChar *input, const PathChar *search, PathChar *output,
                                   std::size_t capacity)
{
    try
    {
        PathInfo info;
        if (std::strchr(input, '/'))
            return Info(input, &info) == Status::Found && !info.directory ? FullPath(input, output, capacity) : 0;
        if (!search)
            search = std::getenv("PATH");
        std::string paths = search ? search : "";
        std::size_t start = 0;
        do
        {
            auto end = paths.find(':', start);
            if (end == std::string::npos)
                end = paths.size();
            auto directory = paths.substr(start, end - start);
            auto candidate = (directory.empty() ? "." : directory) + '/' + input;
            if (Info(candidate.c_str(), &info) == Status::Found && !info.directory)
                return FullPath(candidate.c_str(), output, capacity);
            start = end + 1;
        } while (start <= paths.size());
        return 0;
    }
    catch (...)
    {
        return 0;
    }
}
std::size_t KauaiFiles::TempPath(PathChar *output, std::size_t capacity)
{
    try
    {
        const char *configured = std::getenv("TMPDIR");
        auto path = Normalize(configured && *configured ? configured : "/tmp");
        if (path.empty())
            return 0;
        if (path.back() != '/')
            path += '/';
        return Copy(path, output, capacity);
    }
    catch (...)
    {
        return 0;
    }
}
std::uint32_t KauaiFiles::RootMask()
{
    return 1;
}
std::uint32_t KauaiFiles::SuppressMediaErrors()
{
    return 0;
}
void KauaiFiles::RestoreMediaErrors(std::uint32_t)
{
}
bool KauaiFiles::RootPath(unsigned index, PathChar *output, std::size_t capacity)
{
    return index == 0 && Copy("/", output, capacity) != 0;
}
std::uint32_t KauaiFiles::VolumeKinds(const PathChar *)
{
    return 0;
}
KauaiFiles::Iteration KauaiFiles::First(const PathChar *pattern, DirectoryHandle *handle, DirectoryEntry *entry)
{
    *handle = nullptr;
    try
    {
        std::string path(pattern);
        if (path.empty() || path.back() != '*')
            return Iteration::Error;
        path.pop_back();
        auto *directory = new (std::nothrow) Directory{nullptr, path};
        if (!directory)
            return Iteration::Error;
        directory->stream = opendir(path.c_str());
        if (!directory->stream)
        {
            delete directory;
            return Iteration::Error;
        }
        *handle = directory;
        return Next(*handle, entry);
    }
    catch (...)
    {
        return Iteration::Error;
    }
}
KauaiFiles::Iteration KauaiFiles::Next(DirectoryHandle handle, DirectoryEntry *entry)
{
    auto *directory = static_cast<Directory *>(handle);
    if (!directory)
        return Iteration::Error;
    errno = 0;
    dirent *item = readdir(directory->stream);
    if (!item)
        return errno ? Iteration::Error : Iteration::End;
    if (!Copy(item->d_name, entry->name, sizeof(entry->name)))
        return Iteration::Error;
    try
    {
        auto path = directory->path + item->d_name;
        return Info(path.c_str(), &entry->info) == Status::Found ? Iteration::Entry : Iteration::Error;
    }
    catch (...)
    {
        return Iteration::Error;
    }
}
void KauaiFiles::CloseDirectory(DirectoryHandle handle)
{
    auto *directory = static_cast<Directory *>(handle);
    if (directory)
    {
        closedir(directory->stream);
        delete directory;
    }
}
