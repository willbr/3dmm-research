/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dib_frame.h"
#include <cstring>
#include <new>
#include <stdexcept>
namespace KauaiPixels
{
namespace
{
std::uint32_t U32(const std::uint8_t *p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::int32_t I32(const std::uint8_t *p)
{
    const auto raw = U32(p);
    std::int32_t value;
    std::memcpy(&value, &raw, 4);
    return value;
}
struct Header
{
    std::int32_t width, height, depth, pitch;
    bool bottomUp;
    std::size_t bytes;
};
bool Parse(const void *data, std::size_t bytes, Header &result)
{
    if (!data || bytes < 40)
        return false;
    const auto p = static_cast<const std::uint8_t *>(data);
    const auto width = I32(p + 4), height = I32(p + 8);
    const auto depth = unsigned(p[14]) | (unsigned(p[15]) << 8);
    if (U32(p) != 40 || width <= 0 || width > INT32_MAX / 4 || !height || height == INT32_MIN || p[12] != 1 || p[13] ||
        (depth != 24 && depth != 32) || U32(p + 16) != 0 || U32(p + 32) != 0)
        return false;
    const auto rows = static_cast<std::uint32_t>(height < 0 ? -height : height);
    if (std::uint64_t(width) * 4 * rows > INT32_MAX)
        return false;
    const auto pitch = ((std::uint64_t(width) * depth + 31) / 32) * 4;
    const auto count = pitch * rows;
    if (pitch > INT32_MAX || count > INT32_MAX || count > SIZE_MAX - 40 || (U32(p + 20) && U32(p + 20) < count))
        return false;
    result = {width,
              static_cast<std::int32_t>(rows),
              static_cast<std::int32_t>(depth),
              static_cast<std::int32_t>(pitch),
              height > 0,
              static_cast<std::size_t>(count) + 40};
    return true;
}
} // namespace
bool DibFrameBytes(const void *header, std::size_t available, std::size_t &requiredBytes)
{
    Header parsed;
    if (!Parse(header, available, parsed))
        return false;
    requiredBytes = parsed.bytes;
    return true;
}
bool DibFrame::Read(const void *data, std::size_t bytes)
{
    Header source;
    if (!Parse(data, bytes, source) || bytes < source.bytes)
        return false;
    try
    {
        DibFrame result;
        result.width = source.width;
        result.height = source.height;
        result.sourceDepth = source.depth;
        result.pitch = source.width * 4;
        result.pixels.assign(static_cast<std::size_t>(result.pitch) * result.height, 0);
        const auto first = static_cast<const std::uint8_t *>(data) + 40;
        for (std::int32_t y = 0; y < result.height; ++y)
        {
            const auto row = first + std::size_t(source.bottomUp ? result.height - y - 1 : y) * source.pitch;
            auto output = result.pixels.data() + std::size_t(y) * result.pitch;
            for (std::int32_t x = 0; x < result.width; ++x)
                std::memcpy(output + std::size_t(x) * 4, row + std::size_t(x) * (source.depth / 8), source.depth / 8);
        }
        *this = std::move(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    catch (const std::length_error &)
    {
        return false;
    }
}
} // namespace KauaiPixels
