/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_buffer.h"
#include <atomic>

namespace KauaiPixels
{
namespace
{
std::atomic<std::uint64_t> liveBuffers{0}, liveBytes{0}, allocations{0};
}
BufferStatistics PixelBuffers()
{
    return {liveBuffers.load(std::memory_order_relaxed), liveBytes.load(std::memory_order_relaxed),
            allocations.load(std::memory_order_relaxed)};
}
PixelBuffer::~PixelBuffer()
{
    Reset();
}
bool PixelBuffer::Init(std::int32_t width, std::int32_t height, std::int32_t depth)
{
    const auto oldBytes = storage.bytes;
    const bool hadStorage = storage.surface.pixels != nullptr;
    if (!mm_pixel_buffer_init(&storage, width, height, depth, 4))
        return false;
    if (!hadStorage)
        liveBuffers.fetch_add(1, std::memory_order_relaxed);
    liveBytes.fetch_add(storage.bytes, std::memory_order_relaxed);
    liveBytes.fetch_sub(oldBytes, std::memory_order_relaxed);
    allocations.fetch_add(1, std::memory_order_relaxed);
    return true;
}
void PixelBuffer::Reset()
{
    if (storage.surface.pixels)
    {
        liveBuffers.fetch_sub(1, std::memory_order_relaxed);
        liveBytes.fetch_sub(storage.bytes, std::memory_order_relaxed);
    }
    mm_pixel_buffer_reset(&storage);
}
} // namespace KauaiPixels
