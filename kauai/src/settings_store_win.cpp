/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "settings_store_win.h"
#define NOMINMAX
#include <windows.h>
#include <cstdlib>
#include <string>

namespace
{
using namespace KauaiSettings;
#ifdef UNICODE
using NativeChar = wchar_t;
#else
using NativeChar = char;
#endif
const NativeChar *Native(const NameChar *text)
{
    static_assert(sizeof(NativeChar) == sizeof(NameChar), "settings character width mismatch");
    return reinterpret_cast<const NativeChar *>(text);
}
Handle Open(const NameChar *key, bool machine, bool *created)
{
    HKEY handle;
    DWORD disposition;
    HKEY root = machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    HKEY isolated = nullptr;
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    if (const auto run = std::getenv("KAUAI_TEST_SETTINGS_RUN"))
    {
        const std::string id(run);
        if (id.empty() || id.size() > 64 || id.find_first_not_of("0123456789abcdef-") != std::string::npos)
            return nullptr;
        const auto path = "Software\\3DMMForever\\RetailOracle\\SDL-" + id + (machine ? "\\Machine" : "\\User");
        if (RegCreateKeyExA(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, nullptr,
                            &isolated, nullptr) != ERROR_SUCCESS)
            return nullptr;
        root = isolated;
    }
#endif
    const auto result = RegCreateKeyEx(root, Native(key), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                       KEY_ALL_ACCESS | KEY_WOW64_32KEY, nullptr, &handle, &disposition);
    if (isolated)
        RegCloseKey(isolated);
    if (result != ERROR_SUCCESS)
        return nullptr;
    *created = disposition == REG_CREATED_NEW_KEY;
    return handle;
}
ReadResult Read(Handle handle, const NameChar *name, void *data, std::uint32_t *bytes, Type *type)
{
    DWORD nativeBytes = *bytes, nativeType = 0;
    const auto result = RegQueryValueEx(static_cast<HKEY>(handle), Native(name), nullptr, &nativeType,
                                        static_cast<BYTE *>(data), &nativeBytes);
    *bytes = nativeBytes;
    if (result != ERROR_SUCCESS)
        return result == ERROR_FILE_NOT_FOUND ? ReadResult::Missing : ReadResult::Error;
    *type = nativeType == REG_DWORD    ? Type::Number
            : nativeType == REG_SZ     ? Type::String
            : nativeType == REG_BINARY ? Type::Binary
                                       : Type::Unknown;
    return ReadResult::Found;
}
bool Write(Handle handle, const NameChar *name, const void *data, std::uint32_t bytes, Type type)
{
    const DWORD nativeType = type == Type::Number   ? REG_DWORD
                             : type == Type::String ? REG_SZ
                             : type == Type::Binary ? REG_BINARY
                                                    : REG_NONE;
    return RegSetValueEx(static_cast<HKEY>(handle), Native(name), 0, nativeType, static_cast<const BYTE *>(data),
                         bytes) == ERROR_SUCCESS;
}
void Close(Handle handle)
{
    RegCloseKey(static_cast<HKEY>(handle));
}
} // namespace
const KauaiSettings::Backend &KauaiSettings::WindowsBackend()
{
    static const Backend backend{Open, Read, Write, Close};
    return backend;
}
