/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef CORE_PLATFORM_H
#define CORE_PLATFORM_H

// Include before legacy utility macros. This header needs no Windows, SDL or UI.
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include "mm/native_runtime.h"

class Mutex
{
    // Existing callers enter recursively, including debug allocator checks.
    MMNativeMutex *_mutex;
    // Retain the native facade's diagnostic allocation/layout profile while
    // C owns the synchronization state. No std mutex object is constructed.
    unsigned char _reserved[sizeof(std::recursive_mutex) - sizeof(_mutex)];
    static_assert(alignof(std::recursive_mutex) == alignof(MMNativeMutex *), "legacy mutex alignment");

  public:
    Mutex();
    ~Mutex();
    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;
    void Enter();
    void Leave();
};
typedef Mutex *PMutex;

namespace KauaiPlatform
{
// Coalescing auto-reset event. One Signal releases one waiter; a signal is
// retained if no waiter is present. UINT32_MAX waits indefinitely.
class AutoResetEvent
{
    struct LegacyLayout {
        std::mutex mutex;
        std::condition_variable changed;
        bool signaled;
    };
    MMNativeEvent *event;
    unsigned char reserved[sizeof(LegacyLayout) - sizeof(event)];
    static_assert(alignof(LegacyLayout) == alignof(MMNativeEvent *), "legacy event alignment");

  public:
    AutoResetEvent();
    ~AutoResetEvent();
    AutoResetEvent(const AutoResetEvent &) = delete;
    AutoResetEvent &operator=(const AutoResetEvent &) = delete;
    void Signal();
    bool Wait(std::uint32_t milliseconds = UINT32_MAX);
};
// All return null on failure. Zero-byte allocations remain freeable/resizable.
// Reallocate leaves the original block intact on failure, including new size 0.
void *Allocate(std::size_t bytes, bool clear = false);
void *Reallocate(void *block, std::size_t bytes);
void Free(void *block);
std::size_t AllocationSize(const void *block);

// Process-local diagnostic identity, not an OS thread ID or a persisted value.
long ThreadToken();
void BreakIntoDebugger();

// Millisecond ticks wrap at 32 bits, matching the legacy Windows clock contract.
using TickSource = std::uint32_t (*)();
std::uint32_t SystemTicks();
// Configure with clocks/users stopped; existing clock bases are not rebased.
// The callback must outlive all users. Null restores the steady-clock default.
TickSource SetTickSource(TickSource source);
} // namespace KauaiPlatform
#endif
