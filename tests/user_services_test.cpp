/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "platform_users.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error User services must not import the Windows SDK
#endif
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winnetwk.h>
#include <shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif
using namespace KauaiUsers;
static void CheckAt(bool ok, int line)
{
    if (!ok)
    {
        std::fprintf(stderr, "User service check failed at line %d\n", line);
        std::exit(1);
    }
}
#define Check(ok) CheckAt((ok), __LINE__)
#ifndef _WIN32
struct Environment
{
    const char *name;
    bool existed;
    std::string previous;
    Environment(const char *key, const char *value) : name(key), existed(std::getenv(key) != nullptr)
    {
        if (existed)
            previous = std::getenv(key);
        Check(setenv(key, value, 1) == 0);
    }
    ~Environment()
    {
        if (existed)
            setenv(name, previous.c_str(), 1);
        else
            unsetenv(name);
    }
};
#endif
int main()
{
    Check(PosixDataRoot(PosixHost::Linux, "/home/user", "/data/") == "/data/3dmmforever/Users");
    Check(PosixDataRoot(PosixHost::Linux, "/home/user/", "relative") == "/home/user/.local/share/3dmmforever/Users");
    Check(PosixDataRoot(PosixHost::Linux, "/home/user", "") == "/home/user/.local/share/3dmmforever/Users");
    Check(PosixDataRoot(PosixHost::Linux, nullptr, "/data") == "/data/3dmmforever/Users");
    Check(PosixDataRoot(PosixHost::Linux, "relative", nullptr).empty());
    Check(PosixDataRoot(PosixHost::MacOS, "/Users/user", "/ignored") ==
          "/Users/user/Library/Application Support/3DMMForever/Users");
    Check(PosixDataRoot(PosixHost::MacOS, nullptr, "/ignored").empty());
#ifndef _WIN32
    // These variables exist only in this test process; no directories are made.
    Environment home("HOME", "/user-fixture"), data("XDG_DATA_HOME", "/data-fixture");
    Environment user("USER", "not-the-account"), login("LOGNAME", "not-the-account");
#endif
    const auto name = QueryName();
    Name expectedRoot;
#ifdef _WIN32
#ifdef UNICODE
    using NativeChar = wchar_t;
#else
    using NativeChar = char;
#endif
    NativeChar native[256] = {};
    DWORD capacity = 256;
    const auto status = WNetGetUser(nullptr, native, &capacity);
    if (status == NO_ERROR && native[0])
        Check(name.status == NameStatus::Found && name.name == reinterpret_cast<const NameChar *>(native));
    else if (status != ERROR_EXTENDED_ERROR)
        Check(name.status == NameStatus::Unavailable);
    NativeChar local[MAX_PATH];
    Check(SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local) == S_OK);
    expectedRoot = reinterpret_cast<const NameChar *>(local);
    for (const char *p = "\\Microsoft Kids\\Users"; *p; ++p)
        expectedRoot += static_cast<NameChar>(*p);
#else
    const auto account = getpwuid(geteuid());
    if (account && account->pw_name && account->pw_name[0] && std::strlen(account->pw_name) <= 255)
        Check(name.status == NameStatus::Found && name.name == account->pw_name);
    else
        Check(name.status == NameStatus::Unavailable);
    if (const auto expected = std::getenv("KAUAI_TEST_ACCOUNT"))
        Check(name.status == NameStatus::Found && name.name == expected);
#ifdef __APPLE__
    expectedRoot = "/user-fixture/Library/Application Support/3DMMForever/Users";
#else
    expectedRoot = "/data-fixture/3dmmforever/Users";
#endif
#endif
    if (name.status == NameStatus::Found)
        Check(!name.name.empty() && name.name.length() <= 255);
    NameChar output[1024];
    const auto length = DataRoot(output, 1024);
    Check(length == expectedRoot.length() && Name(output) == expectedRoot);
    Check(!DataRoot(nullptr, 1024));
    NameChar untouched = static_cast<NameChar>('x');
    Check(!DataRoot(&untouched, 0) && untouched == static_cast<NameChar>('x'));
    Check(!DataRoot(output, 1) && !output[0]);
    Check(!DataRoot(output, length) && !output[0]);
    Check(DataRoot(output, length + 1) == length && Name(output) == expectedRoot);
    std::puts(
        "User services: native account lookup, unchanged Windows root, POSIX path policy and buffer limits passed.");
    return 0;
}
