/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_buffer.h"
#include <atomic>
#include <new>
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
bool PixelBuffer::Init(std::int32_t w, std::int32_t h, std::int32_t bits)
{
    if (w <= 0 || h <= 0 || (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32))
        return false;
    const auto row = ((std::uint64_t(w) * bits + 31) / 32) * 4;
    const auto count = row * h;
    if (row > INT32_MAX || count > INT32_MAX || count > SIZE_MAX)
        return false;
    try
    {
        std::unique_ptr<std::uint8_t[]> allocated(new std::uint8_t[static_cast<std::size_t>(count)]{});
        if (!pixels)
            liveBuffers.fetch_add(1, std::memory_order_relaxed);
        liveBytes.fetch_add(count, std::memory_order_relaxed);
        liveBytes.fetch_sub(bytes, std::memory_order_relaxed);
        allocations.fetch_add(1, std::memory_order_relaxed);
        pixels.swap(allocated);
        width = w;
        height = h;
        depth = bits;
        pitch = static_cast<std::int32_t>(row);
        bytes = static_cast<std::size_t>(count);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
void PixelBuffer::Reset()
{
    if (pixels)
    {
        liveBuffers.fetch_sub(1, std::memory_order_relaxed);
        liveBytes.fetch_sub(bytes, std::memory_order_relaxed);
    }
    pixels.reset();
    width = height = depth = pitch = 0;
    bytes = 0;
}
} // namespace KauaiPixels
