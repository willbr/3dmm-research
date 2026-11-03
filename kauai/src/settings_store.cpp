/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "settings_store.h"
#include <cstring>

bool KauaiSettings::Access(const Backend &backend, const NameChar *key, const NameChar *name, void *data,
                           std::uint32_t capacity, const Request &request, bool *noValue, bool *wrongType)
{
    if (wrongType)
        *wrongType = false;
    if (!key || !name || (!data && capacity) || !backend.open || !backend.read || !backend.write || !backend.close)
        return false;
    bool created = false;
    const auto handle = backend.open(key, request.machine, &created);
    if (!handle)
        return false;
    struct Close
    {
        const Backend &backend;
        Handle handle;
        ~Close()
        {
            backend.close(handle);
        }
    } close{backend, handle};

    const bool write = request.write || (created && request.setDefault);
    if (!write)
    {
        auto bytes = capacity;
        Type type = Type::Unknown;
        const auto result = backend.read(handle, name, data, &bytes, &type);
        if (result == ReadResult::Found)
        {
            if (wrongType)
                *wrongType = type != request.type;
            if (noValue)
                *noValue = false;
            return true;
        }
        if (result != ReadResult::Missing || !request.setDefault)
        {
            if (noValue)
                *noValue = result == ReadResult::Missing;
            return noValue && result == ReadResult::Missing;
        }
    }
    auto bytes = capacity;
    if (request.type == Type::Number && capacity != sizeof(std::uint32_t))
        return false;
    if (request.type == Type::String && !request.write)
    {
        bool terminated = false;
        for (std::uint32_t offset = 0; capacity - offset >= sizeof(NameChar); offset += sizeof(NameChar))
        {
            NameChar ch;
            std::memcpy(&ch, static_cast<const unsigned char *>(data) + offset, sizeof(ch));
            if (!ch)
            {
                bytes = offset + sizeof(ch);
                terminated = true;
                break;
            }
        }
        if (!terminated)
            return false;
    }
    if (!backend.write(handle, name, data, bytes, request.type))
        return false;
    if (noValue)
        *noValue = false;
    return true;
}
