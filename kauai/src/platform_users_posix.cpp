/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "platform_users.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <pwd.h>
#include <unistd.h>
static_assert(sizeof(KauaiUsers::NameChar) == 1, "POSIX user paths require byte strings");
KauaiUsers::UserName KauaiUsers::QueryName()
{
    try
    {
        std::vector<char> buffer(1024);
        passwd entry, *found = nullptr;
        int error;
        while ((error = getpwuid_r(geteuid(), &entry, buffer.data(), buffer.size(), &found)) == ERANGE)
        {
            if (buffer.size() >= 1024 * 1024)
                return {};
            buffer.resize(buffer.size() * 2);
        }
        if (error || !found || !found->pw_name || !found->pw_name[0] || std::strlen(found->pw_name) > 255)
            return {};
        UserName result;
        result.name = found->pw_name;
        result.status = NameStatus::Found;
        return result;
    }
    catch (...)
    {
        return {};
    }
}
std::size_t KauaiUsers::DataRoot(NameChar *output, std::size_t capacity)
{
    if (!output || !capacity)
        return 0;
    output[0] = 0;
    try
    {
#ifdef __APPLE__
        const auto host = PosixHost::MacOS;
#else
        const auto host = PosixHost::Linux;
#endif
        const auto root = PosixDataRoot(host, std::getenv("HOME"), std::getenv("XDG_DATA_HOME"));
        if (root.empty() || root.length() >= capacity)
            return 0;
        std::memcpy(output, root.c_str(), root.length() + 1);
        return root.length();
    }
    catch (...)
    {
        return 0;
    }
}
