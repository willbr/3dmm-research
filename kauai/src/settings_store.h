/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SETTINGS_STORE_H
#define KAUAI_SETTINGS_STORE_H
#include <cstdint>

namespace KauaiSettings
{
#ifdef UNICODE
using NameChar = std::uint16_t;
#else
using NameChar = char;
#endif
enum class Type
{
    Number,
    String,
    Binary,
    Unknown
};
enum class ReadResult
{
    Found,
    Missing,
    Error
};
using Handle = void *;
// Backend handles are owned by Access for one operation and always closed.
// Read reports the stored type and actual byte count; sizes are bytes, including
// the terminator for strings. Neither layer converts stored payload encodings.
struct Backend
{
    Handle (*open)(const NameChar *key, bool machine, bool *created);
    ReadResult (*read)(Handle, const NameChar *name, void *data, std::uint32_t *bytes, Type *type);
    bool (*write)(Handle, const NameChar *name, const void *data, std::uint32_t bytes, Type type);
    void (*close)(Handle);
};
struct Request
{
    Type type = Type::Number;
    bool write = false;
    bool setDefault = false;
    bool machine = false;
};
// Legacy Studio get/set/default policy, independent of registry or file APIs.
// A missing value is successful only when noValue is supplied (set to true).
// Successful operations clear noValue; read errors clear it, while open/write
// errors leave it untouched. Defaults are written only for a missing value or
// a newly created store, never for a read error. Explicit string writes retain
// the supplied byte count; string defaults write through the first terminator.
// A found value with a different type still succeeds, reporting wrongType so
// the application can retain its existing diagnostic policy.
bool Access(const Backend &backend, const NameChar *key, const NameChar *name, void *data, std::uint32_t capacity,
            const Request &request, bool *noValue = nullptr, bool *wrongType = nullptr);
} // namespace KauaiSettings
#endif
