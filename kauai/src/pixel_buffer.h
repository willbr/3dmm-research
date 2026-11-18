/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_PIXEL_BUFFER_H
#define KAUAI_PIXEL_BUFFER_H
#include <cstdint>
#include <cstddef>
#include "mm/pixel_storage.h"
namespace KauaiPixels
{
struct BufferStatistics
{
    std::uint64_t liveBuffers, liveBytes, allocations;
};
BufferStatistics PixelBuffers();
// Owned top-down packed pixels, with four-byte row alignment matching Kauai's
// existing bitmap pitches. Palette metadata is kept by the caller. No native
// bitmap, device context, window or SDL resource owns this storage.
class PixelBuffer
{
    MMPixelBuffer storage{};

  public:
    PixelBuffer() = default;
    ~PixelBuffer();
    PixelBuffer(const PixelBuffer &) = delete;
    PixelBuffer &operator=(const PixelBuffer &) = delete;
    // Supports 1/4/8/16/24/32 bits. New bytes are zeroed; failure leaves all
    // previous storage and metadata unchanged. Counts fit legacy signed sizes.
    bool Init(std::int32_t width, std::int32_t height, std::int32_t depth);
    void Reset();
    void *Pixels()
    {
        return storage.surface.pixels;
    }
    const void *Pixels() const
    {
        return storage.surface.pixels;
    }
    std::int32_t Width() const
    {
        return storage.surface.width;
    }
    std::int32_t Height() const
    {
        return storage.surface.height;
    }
    std::int32_t Depth() const
    {
        return storage.surface.depth;
    }
    std::int32_t Pitch() const
    {
        return storage.surface.pitch;
    }
    std::size_t Bytes() const
    {
        return storage.bytes;
    }
};
} // namespace KauaiPixels
#endif
