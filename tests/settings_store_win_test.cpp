/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "settings_store_win.h"
#include <cstdio>
#include <cstring>
#include <string>
#define NOMINMAX
#include <windows.h>
using namespace KauaiSettings;
#define Check(ok)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(ok))                                                                                                     \
        {                                                                                                              \
            std::fprintf(stderr, "Settings registry check failed at line %d\n", __LINE__);                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while (false)
static std::basic_string<NameChar> Name(const char *text)
{
    std::basic_string<NameChar> result;
    while (*text)
        result += static_cast<NameChar>(*text++);
    return result;
}
static bool NativeValue(const char *key, void *data, DWORD *bytes, DWORD *type)
{
    HKEY handle;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &handle) != ERROR_SUCCESS)
        return false;
    const auto result = RegQueryValueEx(handle, TEXT("value"), nullptr, type, static_cast<BYTE *>(data), bytes);
    RegCloseKey(handle);
    return result == ERROR_SUCCESS;
}
static bool Run()
{
    const auto keyText = "Software\\3DMMForever\\Tests\\Settings-" + std::to_string(GetCurrentProcessId()) + "-" +
                         std::to_string(GetTickCount64());
    struct Cleanup
    {
        const std::string &key;
        bool owned = false;
        ~Cleanup()
        {
            if (owned)
                RegDeleteKeyExA(HKEY_CURRENT_USER, key.c_str(), KEY_WOW64_32KEY, 0);
        }
    } cleanup{keyText};
    const auto key = Name(keyText.c_str()), name = Name("value"), absent = Name("absent");
    const auto &backend = WindowsBackend();
    bool created = false;
    auto handle = backend.open(key.c_str(), false, &created);
    Check(handle != nullptr);
    backend.close(handle);
    cleanup.owned = created;
    Check(created); // Never reuse or delete a preexisting test key.

    Request request;
    std::uint32_t value = 0x10203040;
    bool missing = false;
    Check(Access(backend, key.c_str(), absent.c_str(), &value, sizeof(value), request, &missing) && missing);
    Check(!Access(backend, key.c_str(), absent.c_str(), &value, sizeof(value), request));
    request.setDefault = true;
    Check(Access(backend, key.c_str(), name.c_str(), &value, sizeof(value), request, &missing) && !missing);
    DWORD bytes = sizeof(value), type = 0;
    std::uint32_t nativeValue = 0;
    Check(NativeValue(keyText.c_str(), &nativeValue, &bytes, &type));
    Check(nativeValue == value && bytes == 4 && type == REG_DWORD);
    value = 99;
    Check(Access(backend, key.c_str(), name.c_str(), &value, sizeof(value), request) && value == 0x10203040);

    // Some legacy warning flags passed sizeof(bool) for REG_DWORD. Reading
    // those values into an initialized 32-bit flag must preserve their value
    // without eagerly rewriting the stored byte count.
    const unsigned char legacyOff = 0;
    handle = backend.open(key.c_str(), false, &created);
    Check(handle != nullptr);
    const bool legacyWritten = backend.write(handle, name.c_str(), &legacyOff, 1, Type::Number);
    backend.close(handle);
    Check(legacyWritten);
    value = 1;
    Check(Access(backend, key.c_str(), name.c_str(), &value, sizeof(value), request) && value == 0);
    bytes = sizeof(nativeValue);
    Check(NativeValue(keyText.c_str(), &nativeValue, &bytes, &type) && bytes == 1 && type == REG_DWORD);
    request.write = true;
    value = 1;
    Check(Access(backend, key.c_str(), name.c_str(), &value, sizeof(value), request));
    bytes = sizeof(nativeValue);
    Check(NativeValue(keyText.c_str(), &nativeValue, &bytes, &type) && bytes == 4 && nativeValue == 1);

    unsigned char blob[] = {0, 255, 3, 0, 42};
    request.type = Type::Binary;
    request.write = true;
    Check(Access(backend, key.c_str(), name.c_str(), blob, sizeof(blob), request));
    unsigned char nativeBlob[sizeof(blob)] = {};
    bytes = sizeof(nativeBlob);
    Check(NativeValue(keyText.c_str(), nativeBlob, &bytes, &type));
    Check(type == REG_BINARY && bytes == sizeof(blob) && !std::memcmp(blob, nativeBlob, sizeof(blob)));
    request.write = false;
    unsigned char shortBuffer = 0;
    missing = true;
    Check(!Access(backend, key.c_str(), name.c_str(), &shortBuffer, 1, request, &missing) && !missing);
    bytes = sizeof(nativeBlob);
    Check(NativeValue(keyText.c_str(), nativeBlob, &bytes, &type) && bytes == sizeof(blob));
    bool wrongType = false;
    request.type = Type::Number;
    Check(Access(backend, key.c_str(), name.c_str(), nativeBlob, sizeof(nativeBlob), request, nullptr, &wrongType));
    Check(wrongType);

    NameChar text[] = {'a', 0, 'z', 0};
    request.type = Type::String;
    Check(Access(backend, key.c_str(), absent.c_str(), text, sizeof(text), request));
    NameChar copy[4] = {};
    auto stringHandle = backend.open(key.c_str(), false, &created);
    Check(stringHandle != nullptr && !created);
    Type storedType = Type::Unknown;
    std::uint32_t stringBytes = sizeof(copy);
    const auto read = backend.read(stringHandle, absent.c_str(), copy, &stringBytes, &storedType);
    backend.close(stringHandle);
    Check(read == ReadResult::Found && storedType == Type::String && stringBytes == 2 * sizeof(NameChar));
    Check(copy[0] == 'a' && copy[1] == 0);
    request.write = true;
    Check(Access(backend, key.c_str(), name.c_str(), text, sizeof(text), request));
    bytes = sizeof(copy);
    Check(NativeValue(keyText.c_str(), copy, &bytes, &type));
    Check(type == REG_SZ && bytes == sizeof(text) && !std::memcmp(copy, text, sizeof(text)));
    Check(RegDeleteKeyExA(HKEY_CURRENT_USER, keyText.c_str(), KEY_WOW64_32KEY, 0) == ERROR_SUCCESS);
    cleanup.owned = false;
    std::puts("Settings registry: isolated HKCU key, 32-bit view, DWORD/string/binary bytes and cleanup passed.");
    return true;
}
int main()
{
    return Run() ? 0 : 1;
}
