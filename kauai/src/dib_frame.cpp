/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dib_frame.h"
#include "mm/dib_frame.h"
#include <new>
#include <stdexcept>
#include <utility>

namespace KauaiPixels
{
bool DibFrameBytes(const void *header, std::size_t available, std::size_t &requiredBytes)
{
    MMDibInfo info;
    if (!mm_dib_info(header, available, &info))
        return false;
    requiredBytes = info.bytes;
    return true;
}

bool DibFrame::Read(const void *data, std::size_t bytes)
{
    MMDibInfo info;
    if (!mm_dib_info(data, bytes, &info) || bytes < info.bytes)
        return false;
    try
    {
        DibFrame result;
        result.width = info.width;
        result.height = info.height;
        result.sourceDepth = info.depth;
        result.pitch = info.width * 4;
        result.pixels.resize(static_cast<std::size_t>(result.pitch) * result.height);
        const MMPixelSurface target = {result.pixels.data(), result.width, result.height, result.pitch, 32};
        if (!mm_dib_decode(data, bytes, &target))
            return false;
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
