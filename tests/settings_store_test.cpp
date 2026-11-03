/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "settings_store.h"
#include "settings_store_posix.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Settings policy must not import the Windows SDK
#endif
using namespace KauaiSettings;
static void CheckAt(bool ok, int line)
{
    if (!ok)
    {
        std::fprintf(stderr, "Settings policy check failed at line %d\n", line);
        std::exit(1);
    }
}
#define Check(ok) CheckAt((ok), __LINE__)
struct State
{
    bool openOk = true, writeOk = true, readError = false, created = false, exists = false, machine = false;
    unsigned opens = 0, reads = 0, writes = 0, closes = 0;
    Type type = Type::Unknown;
    std::vector<unsigned char> data;
};
static State state;
static Handle Open(const NameChar *, bool machine, bool *created)
{
    ++state.opens;
    state.machine = machine;
    *created = state.created;
    return state.openOk ? &state : nullptr;
}
static ReadResult Read(Handle handle, const NameChar *, void *data, std::uint32_t *bytes, Type *type)
{
    Check(handle == &state);
    ++state.reads;
    if (state.readError)
        return ReadResult::Error;
    if (!state.exists)
        return ReadResult::Missing;
    if (*bytes < state.data.size())
        return ReadResult::Error;
    std::memcpy(data, state.data.data(), state.data.size());
    *bytes = static_cast<std::uint32_t>(state.data.size());
    *type = state.type;
    return ReadResult::Found;
}
static bool Write(Handle handle, const NameChar *, const void *data, std::uint32_t bytes, Type type)
{
    Check(handle == &state);
    ++state.writes;
    if (!state.writeOk)
        return false;
    state.data.resize(bytes);
    if (bytes)
        std::memcpy(state.data.data(), data, bytes);
    state.type = type;
    state.exists = true;
    return true;
}
static void Close(Handle handle)
{
    Check(handle == &state);
    ++state.closes;
}
int main()
{
    Check(ConfigurationRoot(PosixHost::Linux, false, "/home/user", "/config/") == "/config/3dmmforever");
    Check(ConfigurationRoot(PosixHost::Linux, false, "/home/user/", "relative") == "/home/user/.config/3dmmforever");
    Check(ConfigurationRoot(PosixHost::Linux, false, "/home/user", "") == "/home/user/.config/3dmmforever");
    Check(ConfigurationRoot(PosixHost::Linux, false, nullptr, "/config") == "/config/3dmmforever");
    Check(ConfigurationRoot(PosixHost::Linux, false, "relative", nullptr).empty());
    Check(ConfigurationRoot(PosixHost::MacOS, false, "/Users/user", "/ignored") ==
          "/Users/user/Library/Application Support/3DMMForever");
    Check(ConfigurationRoot(PosixHost::MacOS, false, nullptr, "/ignored").empty());
    Check(ConfigurationRoot(PosixHost::Linux, true, nullptr, nullptr) == "/etc/3dmmforever");
    Check(ConfigurationRoot(PosixHost::MacOS, true, nullptr, nullptr) == "/Library/Application Support/3DMMForever");
    const Backend backend{Open, Read, Write, Close};
    const NameChar key[] = {'k', 0}, name[] = {'v', 0};
    Request request;
    std::uint32_t value = 7;
    bool missing = false;
    Check(!Access(backend, key, name, &value, sizeof(value), request));
    Check(state.reads == 1 && !state.writes && state.closes == 1 && value == 7);
    Check(Access(backend, key, name, &value, sizeof(value), request, &missing) && missing);
    request.setDefault = true;
    Check(Access(backend, key, name, &value, sizeof(value), request, &missing) && !missing);
    Check(state.writes == 1 && state.type == Type::Number && state.data.size() == 4);
    value = 9;
    Check(Access(backend, key, name, &value, sizeof(value), request) && value == 7 && state.writes == 1);
    state.readError = true;
    missing = true;
    Check(!Access(backend, key, name, &value, sizeof(value), request, &missing) && !missing);
    Check(state.writes == 1); // Read errors must never replace a stored preference.
    state = State{};
    state.created = true;
    request.machine = true;
    Check(Access(backend, key, name, &value, sizeof(value), request));
    Check(state.machine && !state.reads && state.writes == 1 && state.closes == 1);
    state = State{};
    state.openOk = false;
    missing = true;
    Check(!Access(backend, key, name, &value, sizeof(value), request, &missing) && missing);
    Check(!state.closes && !state.reads && !state.writes);
    state = State{};
    state.writeOk = false;
    Check(!Access(backend, key, name, &value, sizeof(value), request, &missing) && missing);
    Check(state.closes == 1 && state.writes == 1);

    state = State{};
    request.type = Type::String;
    NameChar text[] = {'a', 0, 'z', 0};
    Check(Access(backend, key, name, text, sizeof(text), request));
    Check(state.type == Type::String && state.data.size() == 2 * sizeof(NameChar));
    request.write = true;
    Check(Access(backend, key, name, text, sizeof(text), request));
    Check(state.data.size() == sizeof(text) && !std::memcmp(text, state.data.data(), sizeof(text)));
    request.write = false;
    unsigned char tooSmall = 0x5a;
    const auto writes = state.writes;
    missing = true;
    Check(!Access(backend, key, name, &tooSmall, 1, request, &missing) && !missing && tooSmall == 0x5a);
    Check(state.writes == writes);
    state = State{};
    NameChar unterminated[] = {'a', 'b'};
    Check(!Access(backend, key, name, unterminated, sizeof(unterminated), request));
    Check(!state.writes && state.closes == 1);

    state = State{};
    request.type = Type::Binary;
    request.write = true;
    unsigned char blob[] = {0, 255, 0, 7};
    Check(Access(backend, key, name, blob, sizeof(blob), request));
    request.write = false;
    unsigned char copy[4] = {};
    Check(Access(backend, key, name, copy, sizeof(copy), request) && !std::memcmp(copy, blob, sizeof(blob)));
    request.type = Type::Number;
    bool wrongType = false;
    Check(Access(backend, key, name, copy, sizeof(copy), request, nullptr, &wrongType) && wrongType);
    Check(state.writes == 1 && state.closes == 3);
    request.write = true;
    Check(!Access(backend, key, name, copy, 1, request) && state.writes == 1 && state.closes == 4);
    std::puts("Settings policy: defaults, missing/error distinction, exact payloads, types and handle cleanup passed.");
    return 0;
}
