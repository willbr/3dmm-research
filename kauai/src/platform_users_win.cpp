/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "platform_users.h"
#include <cstring>
#define NOMINMAX
#include <windows.h>
#include <winnetwk.h>
#include <shlobj.h>
namespace
{
#ifdef UNICODE
using NativeChar = wchar_t;
#else
using NativeChar = char;
#endif
KauaiUsers::Name NativeName(const NativeChar *text)
{
    static_assert(sizeof(NativeChar) == sizeof(KauaiUsers::NameChar), "user name character width mismatch");
    return reinterpret_cast<const KauaiUsers::NameChar *>(text);
}
} // namespace
KauaiUsers::UserName KauaiUsers::QueryName()
{
    UserName result;
    try
    {
        NativeChar text[256] = {};
        DWORD count = 256;
        const auto status = WNetGetUser(nullptr, text, &count);
        if (status == ERROR_EXTENDED_ERROR)
        {
            NativeChar provider[256] = {};
            DWORD error;
            if (WNetGetLastError(&error, text, 256, provider, 256) == NO_ERROR)
            {
                result.status = NameStatus::ProviderError;
                result.provider = NativeName(provider);
                result.message = NativeName(text);
                result.error = error;
            }
            else
                result.status = NameStatus::ProviderErrorUnavailable;
        }
        else if (status == NO_ERROR && text[0])
        {
            result.name = NativeName(text);
            result.status = NameStatus::Found;
        }
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
        NativeChar path[MAX_PATH];
        if (SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path) != S_OK)
            return 0;
        auto root = NativeName(path);
        for (const char *p = "\\Microsoft Kids\\Users"; *p; ++p)
            root += static_cast<NameChar>(*p);
        if (root.length() >= capacity)
            return 0;
        std::memcpy(output, root.c_str(), (root.length() + 1) * sizeof(NameChar));
        return root.length();
    }
    catch (...)
    {
        return 0;
    }
}
