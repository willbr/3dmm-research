/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "settings_store_posix.h"
#include "platform_files.h"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static_assert(sizeof(KauaiSettings::NameChar) == 1, "POSIX settings use native byte names");
namespace
{
using namespace KauaiSettings;
constexpr std::uint32_t maxValueBytes = 16 * 1024 * 1024;
struct Descriptor
{
    int value;
    explicit Descriptor(int fd = -1) : value(fd)
    {
    }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
    ~Descriptor()
    {
        if (value >= 0)
            close(value);
    }
    int Release()
    {
        const int fd = value;
        value = -1;
        return fd;
    }
};
struct Store
{
    Descriptor directory, lock;
    Store(int dir, int lockFile) : directory(dir), lock(lockFile)
    {
    }
};
std::string EncodedName(const char *name, char prefix)
{
    if (!name || std::strlen(name) > 126)
        return {};
    const char hex[] = "0123456789abcdef";
    std::string result{prefix, '-'};
    for (const auto *p = reinterpret_cast<const unsigned char *>(name); *p; ++p)
    {
        auto ch = *p;
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
        result += hex[ch >> 4];
        result += hex[ch & 15];
    }
    return result;
}
Handle Open(const NameChar *key, bool machine, bool *created)
{
    if (!created)
        return nullptr;
    try
    {
#ifdef __APPLE__
        const auto host = PosixHost::MacOS;
#else
        const auto host = PosixHost::Linux;
#endif
        const auto root = ConfigurationRoot(host, machine, std::getenv("HOME"), std::getenv("XDG_CONFIG_HOME"));
        const auto name = EncodedName(key, 'k');
        if (root.empty() || name.empty() || !KauaiFiles::MakeDirectories(root.c_str()))
            return nullptr;
        Descriptor rootFd(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (rootFd.value < 0 || (mkdirat(rootFd.value, name.c_str(), 0700) != 0 && errno != EEXIST))
            return nullptr;
        Descriptor directory(openat(rootFd.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (directory.value < 0)
            return nullptr;
        Descriptor lock(openat(directory.value, ".lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600));
        struct stat info;
        if (lock.value < 0 || fstat(lock.value, &info) != 0 || !S_ISREG(info.st_mode))
            return nullptr;
        int result;
        do
        {
            result = flock(lock.value, LOCK_EX);
        } while (result != 0 && errno == EINTR);
        if (result != 0)
            return nullptr;
        auto store = new (std::nothrow) Store(directory.value, lock.value);
        if (!store)
            return nullptr;
        directory.Release();
        lock.Release();
        // Always query under the lock. Another process may have written its
        // default between namespace creation and our lock acquisition.
        *created = false;
        return store;
    }
    catch (...)
    {
        return nullptr;
    }
}
bool ReadAll(int fd, void *data, std::size_t count)
{
    auto p = static_cast<unsigned char *>(data);
    while (count)
    {
        const auto done = read(fd, p, count);
        if (done < 0 && errno == EINTR)
            continue;
        if (done <= 0)
            return false;
        p += done;
        count -= done;
    }
    return true;
}
bool WriteAll(int fd, const void *data, std::size_t count)
{
    auto p = static_cast<const unsigned char *>(data);
    while (count)
    {
        const auto done = write(fd, p, count);
        if (done < 0 && errno == EINTR)
            continue;
        if (done <= 0)
            return false;
        p += done;
        count -= done;
    }
    return true;
}
bool AtEnd(int fd)
{
    unsigned char extra;
    ssize_t result;
    do
    {
        result = read(fd, &extra, 1);
    } while (result < 0 && errno == EINTR);
    return result == 0;
}
bool Sync(int fd)
{
    int result;
    do
    {
        result = fsync(fd);
    } while (result != 0 && errno == EINTR);
    return result == 0;
}
bool ValidPayload(Type type, const void *data, std::uint32_t bytes)
{
    return bytes <= maxValueBytes && (!bytes || data) &&
           (type == Type::Binary || (type == Type::Number && bytes == 4) ||
            (type == Type::String && bytes && std::memchr(data, 0, bytes)));
}
ReadResult Read(Handle handle, const NameChar *name, void *data, std::uint32_t *bytes, Type *type)
{
    if (!handle || !bytes || !type)
        return ReadResult::Error;
    try
    {
        const auto file = EncodedName(name, 'v');
        if (file.empty())
            return ReadResult::Error;
        Descriptor fd(openat(static_cast<Store *>(handle)->directory.value, file.c_str(),
                             O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        if (fd.value < 0)
            return errno == ENOENT ? ReadResult::Missing : ReadResult::Error;
        struct stat info;
        unsigned char header[12];
        if (fstat(fd.value, &info) != 0 || !S_ISREG(info.st_mode) || !ReadAll(fd.value, header, sizeof(header)) ||
            std::memcmp(header, "KPS1", 4) || header[5] || header[6] || header[7])
            return ReadResult::Error;
        const auto length = std::uint32_t(header[8]) | (std::uint32_t(header[9]) << 8) |
                            (std::uint32_t(header[10]) << 16) | (std::uint32_t(header[11]) << 24);
        if (length > maxValueBytes || info.st_size != static_cast<off_t>(sizeof(header) + length))
            return ReadResult::Error;
        const auto storedType = header[4] == 1   ? Type::Number
                                : header[4] == 2 ? Type::String
                                : header[4] == 3 ? Type::Binary
                                                 : Type::Unknown;
        if (data && *bytes < length)
        {
            *bytes = length;
            return ReadResult::Error;
        }
        std::vector<unsigned char> payload(length);
        if (!ReadAll(fd.value, payload.data(), length) || !AtEnd(fd.value) ||
            !ValidPayload(storedType, payload.data(), length))
            return ReadResult::Error;
        if (data && length)
            std::memcpy(data, payload.data(), length);
        *bytes = length;
        *type = storedType;
        return ReadResult::Found;
    }
    catch (...)
    {
        return ReadResult::Error;
    }
}
bool Write(Handle handle, const NameChar *name, const void *data, std::uint32_t bytes, Type type)
{
    if (!handle || !ValidPayload(type, data, bytes))
        return false;
    try
    {
        const auto file = EncodedName(name, 'v');
        if (file.empty())
            return false;
        const auto directory = static_cast<Store *>(handle)->directory.value;
        struct Temporary
        {
            int directory;
            std::string name;
            Descriptor fd;
            explicit Temporary(int dir) : directory(dir)
            {
            }
            ~Temporary()
            {
                if (!name.empty())
                    unlinkat(directory, name.c_str(), 0);
            }
        } temporary(directory);
        static std::atomic<std::uint64_t> sequence{0};
        for (unsigned attempt = 0; attempt < 128; ++attempt)
        {
            auto candidate = ".tmp-" + std::to_string(getpid()) + "-" +
                             std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
            temporary.fd.value =
                openat(directory, candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            if (temporary.fd.value >= 0)
            {
                temporary.name.swap(candidate);
                break;
            }
            if (errno != EEXIST)
                return false;
        }
        if (temporary.fd.value < 0)
            return false;
        unsigned char header[12] = {
            'K', 'P', 'S', '1', static_cast<unsigned char>(type == Type::Number ? 1 : type == Type::String ? 2 : 3),
            0,   0,   0};
        for (unsigned i = 0; i < 4; ++i)
            header[8 + i] = static_cast<unsigned char>(bytes >> (8 * i));
        if (!WriteAll(temporary.fd.value, header, sizeof(header)) || !WriteAll(temporary.fd.value, data, bytes) ||
            !Sync(temporary.fd.value) || close(temporary.fd.Release()) != 0 ||
            renameat(directory, temporary.name.c_str(), directory, file.c_str()) != 0)
            return false;
        temporary.name.clear();
        Sync(directory); // Best effort: not all filesystems support directory fsync.
        return true;
    }
    catch (...)
    {
        return false;
    }
}
void Close(Handle handle)
{
    delete static_cast<Store *>(handle);
}
} // namespace
const KauaiSettings::Backend &KauaiSettings::PosixBackend()
{
    static const Backend backend{Open, Read, Write, Close};
    return backend;
}
