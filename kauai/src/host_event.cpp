/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "host_event.h"
#include "sdl_host.h"
#include "platform_text.h"
#include "text_utf8.h"
#include <deque>
#include <new>
namespace KauaiInput
{
struct Queue::Impl
{
    std::deque<Event> pending;
};
Queue::Queue() : impl(new Impl)
{
}
Queue::~Queue()
{
    delete impl;
}
bool Queue::Next(KauaiSDL::Host &host, Event &event, bool wait)
{
    event = {};
    if (!impl->pending.empty())
    {
        event = impl->pending.front();
        impl->pending.pop_front();
        return true;
    }
    KauaiSDL::Event input;
    if (!host.Poll(input, wait ? 8 : 0))
        return false;
    event.x = input.x;
    event.y = input.y;
    event.key = input.key;
    event.button = input.button;
    event.modifiers = input.modifiers;
    event.active = input.active;
    event.repeat = input.repeat;
    event.timestamp = input.timestamp;
    using K = KauaiSDL::EventKind;
    switch (input.kind)
    {
    case K::quit:
        event.kind = Kind::quit;
        break;
    case K::exposed:
        event.kind = Kind::exposed;
        break;
    case K::resized:
        event.kind = Kind::resized;
        break;
    case K::focus:
        event.kind = Kind::focus;
        break;
    case K::motion:
        event.kind = Kind::motion;
        break;
    case K::buttonDown:
        event.kind = Kind::buttonDown;
        break;
    case K::buttonUp:
        event.kind = Kind::buttonUp;
        break;
    case K::keyDown:
        event.kind = Kind::keyDown;
        break;
    case K::keyUp:
        event.kind = Kind::keyUp;
        break;
    case K::clipboard:
        event.kind = Kind::clipboard;
        break;
    case K::text:
        try
        {
            std::vector<std::uint16_t> units;
            if (!KauaiText::DecodeUtf8(input.text, units))
                return false;
            const auto count =
                KauaiText::ToAnsi(units.data(), units.size() * 2, KauaiText::Encoding::Utf16LE, nullptr, 0);
            if (!count)
                return false;
            std::string bytes(count, 0);
            if (!KauaiText::ToAnsi(units.data(), units.size() * 2, KauaiText::Encoding::Utf16LE, &bytes[0], count))
                return false;
            std::deque<Event> characters;
            event.kind = Kind::character;
            for (unsigned char byte : bytes)
            {
                event.character = byte;
                characters.push_back(event);
            }
            impl->pending.swap(characters);
            event = impl->pending.front();
            impl->pending.pop_front();
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        break;
    default:
        return false;
    }
    return true;
}
bool Queue::NextKey(KauaiSDL::Host &host, Event &event)
{
    Event next;
    if (!Next(host, next))
        return false;
    if (next.kind == Kind::keyDown || next.kind == Kind::character)
    {
        event = next;
        return true;
    }
    impl->pending.push_front(next);
    return false;
}
bool Queue::TakeCharacter(KauaiSDL::Host &host, std::uint32_t &character)
{
    Event next;
    if (!Next(host, next))
        return false;
    if (next.kind == Kind::character)
    {
        character = next.character;
        return true;
    }
    impl->pending.push_front(next);
    return false;
}
void Queue::Flush(KauaiSDL::Host &host, bool mouse, bool keys)
{
    host.FlushInput(mouse, keys);
    for (auto entry = impl->pending.begin(); entry != impl->pending.end();)
    {
        const auto kind = entry->kind;
        if ((mouse && (kind == Kind::motion || kind == Kind::buttonDown || kind == Kind::buttonUp)) ||
            (keys && (kind == Kind::keyDown || kind == Kind::keyUp || kind == Kind::character)))
            entry = impl->pending.erase(entry);
        else
            ++entry;
    }
}
} // namespace KauaiInput
