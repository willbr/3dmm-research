/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "core_platform.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <limits>

namespace
{
// Preserve malloc alignment while retaining a portable size query for the
// legacy debug heap checks. This header is private and never reaches disk.
union AllocationHeader {
    std::max_align_t alignment;
    std::size_t bytes;
};

bool AllocationBytes(std::size_t bytes, std::size_t *total)
{
    const std::size_t payload = bytes == 0 ? 1 : bytes;
    if (payload > (std::numeric_limits<std::size_t>::max)() - sizeof(AllocationHeader))
        return false;
    *total = payload + sizeof(AllocationHeader);
    return true;
}

std::atomic<KauaiPlatform::TickSource> tickSource{nullptr};
std::atomic<std::uint32_t> nextThreadToken{1};
} // namespace

void KauaiPlatform::AutoResetEvent::Signal()
{
    std::lock_guard<std::mutex> lock(mutex);
    signaled = true;
    changed.notify_one();
}
bool KauaiPlatform::AutoResetEvent::Wait(std::uint32_t milliseconds)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (milliseconds == UINT32_MAX)
        changed.wait(lock, [&] { return signaled; });
    else if (!changed.wait_for(lock, std::chrono::milliseconds(milliseconds), [&] { return signaled; }))
        return false;
    signaled = false;
    return true;
}

void *KauaiPlatform::Allocate(std::size_t bytes, bool clear)
{
    std::size_t total;
    if (!AllocationBytes(bytes, &total))
        return nullptr;
    auto *header = static_cast<AllocationHeader *>(clear ? std::calloc(1, total) : std::malloc(total));
    if (header == nullptr)
        return nullptr;
    header->bytes = bytes;
    return header + 1;
}

void *KauaiPlatform::Reallocate(void *block, std::size_t bytes)
{
    if (block == nullptr)
        return Allocate(bytes);
    std::size_t total;
    if (!AllocationBytes(bytes, &total))
        return nullptr;
    auto *header = static_cast<AllocationHeader *>(std::realloc(static_cast<AllocationHeader *>(block) - 1, total));
    if (header == nullptr)
        return nullptr;
    header->bytes = bytes;
    return header + 1;
}

void KauaiPlatform::Free(void *block)
{
    if (block != nullptr)
        std::free(static_cast<AllocationHeader *>(block) - 1);
}

std::size_t KauaiPlatform::AllocationSize(const void *block)
{
    return block == nullptr ? 0 : (static_cast<const AllocationHeader *>(block) - 1)->bytes;
}

long KauaiPlatform::ThreadToken()
{
    thread_local const auto token = nextThreadToken.fetch_add(1, std::memory_order_relaxed);
    return static_cast<long>(token);
}
void KauaiPlatform::BreakIntoDebugger()
{
#ifdef _MSC_VER
    __debugbreak();
#elif defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#else
    std::abort();
#endif
}

std::uint32_t KauaiPlatform::SystemTicks()
{
    auto source = tickSource.load(std::memory_order_relaxed);
    if (source != nullptr)
        return source();
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

KauaiPlatform::TickSource KauaiPlatform::SetTickSource(TickSource source)
{
    return tickSource.exchange(source, std::memory_order_relaxed);
}
