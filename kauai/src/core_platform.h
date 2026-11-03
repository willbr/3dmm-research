/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef CORE_PLATFORM_H
#define CORE_PLATFORM_H

// Include before legacy utility macros. This header needs no Windows, SDL or UI.
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <condition_variable>

class Mutex
{
    // Existing callers enter recursively, including debug allocator checks.
    std::recursive_mutex _mutex;

  public:
    void Enter()
    {
        _mutex.lock();
    }
    void Leave()
    {
        _mutex.unlock();
    }
};
typedef Mutex *PMutex;

namespace KauaiPlatform
{
// Coalescing auto-reset event. One Signal releases one waiter; a signal is
// retained if no waiter is present. UINT32_MAX waits indefinitely.
class AutoResetEvent
{
    std::mutex mutex;
    std::condition_variable changed;
    bool signaled = false;

  public:
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
