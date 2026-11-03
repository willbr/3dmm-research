/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_HOST_EVENT_H
#define KAUAI_HOST_EVENT_H
#include <cstdint>
namespace KauaiSDL
{
class Host;
}
namespace KauaiInput
{
enum class Kind
{
    none,
    quit,
    exposed,
    resized,
    focus,
    motion,
    buttonDown,
    buttonUp,
    keyDown,
    keyUp,
    character,
    clipboard
};
struct Event
{
    Kind kind = Kind::none;
    float x = 0, y = 0;
    std::uint32_t key = 0, character = 0, button = 0, modifiers = 0;
    std::uint64_t timestamp = 0;
    bool active = false, repeat = false;
};
// SDK-free application input queue. UTF-8 text becomes counted legacy-byte
// character events; key lookahead never discards an intervening event.
class Queue
{
    struct Impl;
    Impl *impl;

  public:
    Queue();
    ~Queue();
    Queue(const Queue &) = delete;
    Queue &operator=(const Queue &) = delete;
    bool Next(KauaiSDL::Host &host, Event &event, bool wait = false);
    bool NextKey(KauaiSDL::Host &host, Event &event);
    bool TakeCharacter(KauaiSDL::Host &host, std::uint32_t &character);
    void Flush(KauaiSDL::Host &host, bool mouse, bool keys);
};
} // namespace KauaiInput
#endif
