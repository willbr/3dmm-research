/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_content.h"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
static_assert(sizeof(StudioContent::Char) == 1, "POSIX content uses native byte paths");
StudioContent::Root StudioContent::ConfiguredRoot()
{
    try
    {
        const char *path = std::getenv("THREEDMM_CONTENT_ROOT");
        return ValidateRoot(path ? path : "", false);
    }
    catch (...)
    {
        return {Status::Error, {}, ENOMEM};
    }
}
StudioContent::Products StudioContent::ReadProducts(const Char *root)
{
    if (!root || !*root)
        return {Status::Invalid, {}, 0};
    try
    {
        std::string path(root);
        if (ValidateRoot(path, false).status != Status::Ready)
            return {Status::Invalid, {}, 0};
        // Require the selected root itself to exist; ENOENT must only mean a
        // missing optional manifest, not a typo in the installation directory.
        struct stat info;
        if (stat(root, &info) != 0)
            return {Status::Error, {}, static_cast<std::uint32_t>(errno)};
        if (!S_ISDIR(info.st_mode))
            return {Status::Invalid, {}, 0};
        if (path.back() != '/')
            path += '/';
        path += "products.tsv";
        struct File
        {
            int fd;
            ~File()
            {
                if (fd >= 0)
                    close(fd);
            }
        } file{open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)};
        if (file.fd < 0)
        {
            const int error = errno;
            if (error == ENOENT)
                return {};
            return {Status::Error, {}, static_cast<std::uint32_t>(error)};
        }
        if (fstat(file.fd, &info) != 0)
            return {Status::Error, {}, static_cast<std::uint32_t>(errno)};
        if (!S_ISREG(info.st_mode) || info.st_size > 1024 * 1024)
            return {Status::Invalid, {}, 0};
        std::string bytes;
        char buffer[4096];
        for (;;)
        {
            const auto count = read(file.fd, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR)
                continue;
            if (count < 0)
                return {Status::Error, {}, static_cast<std::uint32_t>(errno)};
            if (!count)
                break;
            bytes.append(buffer, static_cast<std::size_t>(count));
            if (bytes.length() > 1024 * 1024)
                return {Status::Invalid, {}, 0};
        }
        return ParseProducts(bytes);
    }
    catch (...)
    {
        return {Status::Error, {}, ENOMEM};
    }
}
