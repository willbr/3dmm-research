/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_PIXEL_BUFFER_H
#define KAUAI_PIXEL_BUFFER_H
#include <cstdint>
#include <cstddef>
#include <memory>
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
    std::unique_ptr<std::uint8_t[]> pixels;
    std::int32_t width = 0, height = 0, depth = 0, pitch = 0;
    std::size_t bytes = 0;

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
        return pixels.get();
    }
    const void *Pixels() const
    {
        return pixels.get();
    }
    std::int32_t Width() const
    {
        return width;
    }
    std::int32_t Height() const
    {
        return height;
    }
    std::int32_t Depth() const
    {
        return depth;
    }
    std::int32_t Pitch() const
    {
        return pitch;
    }
    std::size_t Bytes() const
    {
        return bytes;
    }
};
} // namespace KauaiPixels
#endif
