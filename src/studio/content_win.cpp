/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_content_win.h"
#define NOMINMAX
#include <windows.h>
namespace
{
#ifdef UNICODE
using NativeChar = wchar_t;
#else
using NativeChar = char;
#endif
static_assert(sizeof(NativeChar) == sizeof(StudioContent::Char), "content character width mismatch");
StudioContent::Text NativeText(const NativeChar *value, std::size_t count)
{
    return StudioContent::Text(reinterpret_cast<const StudioContent::Char *>(value), count);
}
} // namespace
StudioContent::Root StudioContent::ConfiguredRoot()
{
    NativeChar path[256] = {};
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariable(TEXT("THREEDMM_CONTENT_ROOT"), path, 256);
    if (!length)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_ENVVAR_NOT_FOUND || error == ERROR_SUCCESS)
            return {};
        return {Status::Error, {}, error};
    }
    if (length >= 256)
        return {Status::Invalid, {}, 0};
    try
    {
        return ValidateRoot(NativeText(path, length), true);
    }
    catch (...)
    {
        return {Status::Error, {}, ERROR_NOT_ENOUGH_MEMORY};
    }
}
StudioContent::Products StudioContent::ReadRegistryProducts(const Char *key, bool machine)
{
    if (!key || !*key)
        return {Status::Invalid, {}, 0};
    struct Key
    {
        HKEY handle = nullptr;
        ~Key()
        {
            if (handle)
                RegCloseKey(handle);
        }
    } opened;
    auto status =
        RegOpenKeyEx(machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, reinterpret_cast<const NativeChar *>(key), 0,
                     KEY_READ | KEY_WOW64_32KEY, &opened.handle);
    if (status != ERROR_SUCCESS)
        return {Status::Error, {}, static_cast<std::uint32_t>(status)};
    try
    {
        Products result;
        for (DWORD index = 0;; ++index)
        {
            NativeChar name[256] = {}, title[257] = {};
            DWORD nameCount = 256, bytes = 256 * sizeof(NativeChar), type = 0;
            status = RegEnumValue(opened.handle, index, name, &nameCount, nullptr, &type,
                                  reinterpret_cast<BYTE *>(title), &bytes);
            if (status == ERROR_NO_MORE_ITEMS)
                return result;
            // Oversized or non-string values are not valid Studio titles. Never
            // interpret a failed enumeration's buffers or reuse previous lengths.
            if (status == ERROR_MORE_DATA)
                continue;
            if (status != ERROR_SUCCESS)
                return {Status::Error, {}, static_cast<std::uint32_t>(status)};
            if (type != REG_SZ || bytes % sizeof(NativeChar))
                continue;
            std::size_t length = 0;
            while (length < bytes / sizeof(NativeChar) && title[length])
                ++length;
            if (length > 255)
                continue;
            result.entries.push_back({NativeText(name, nameCount), NativeText(title, length)});
        }
    }
    catch (...)
    {
        return {Status::Error, {}, ERROR_NOT_ENOUGH_MEMORY};
    }
}
StudioContent::Products StudioContent::ReadProducts(const Char *)
{
    return ReadRegistryProducts(
        reinterpret_cast<const Char *>(TEXT("Software\\Microsoft\\Microsoft Kids\\3D Movie Maker\\Products")), true);
}
