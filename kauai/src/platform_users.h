/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_PLATFORM_USERS_H
#define KAUAI_PLATFORM_USERS_H
#include "platform_files.h"
#include <string>
namespace KauaiUsers
{
using NameChar = KauaiFiles::PathChar;
using Name = std::basic_string<NameChar>;
enum class NameStatus
{
    Found,
    Unavailable,
    ProviderError,
    ProviderErrorUnavailable
};
struct UserName
{
    NameStatus status = NameStatus::Unavailable;
    Name name, provider, message;
    std::uint32_t error = 0;
};
// Windows keeps WNetGetUser and its extended-provider-error information.
// POSIX queries the effective uid's account record, not USER/LOGNAME. Names
// longer than Studio's 255-code-unit limit are unavailable, never truncated.
UserName QueryName();
// Writable user-profile fallback root (before the username leaf). Does not
// create anything. Native path code units, length excluding NUL; failure or
// insufficient capacity returns zero and clears nonempty output.
std::size_t DataRoot(NameChar *output, std::size_t capacity);
enum class PosixHost
{
    Linux,
    MacOS
};
// Pure app-data path policy; absolute XDG_DATA_HOME overrides HOME on Linux.
// Relative XDG values are ignored; macOS uses HOME/Library/Application Support.
std::string PosixDataRoot(PosixHost host, const char *home, const char *xdgDataHome);
} // namespace KauaiUsers
#endif
