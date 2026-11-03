/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_DIB_FRAME_H
#define KAUAI_DIB_FRAME_H
#include <cstddef>
#include <cstdint>
#include <vector>
namespace KauaiPixels
{
// Standard 40-byte BI_RGB DIBs with 24-bit BGR or 32-bit BGRX pixels.
// Reads only the bounded header; returns the required contiguous DIB size.
// This also supports legacy decoder APIs which guarantee a complete DIB but
// do not return its byte count. Failure preserves requiredBytes.
bool DibFrameBytes(const void *header, std::size_t available, std::size_t &requiredBytes);
struct DibFrame
{
    std::int32_t width = 0, height = 0, pitch = 0, sourceDepth = 32;
    std::vector<std::uint8_t> pixels; // Owned top-down BGRX32; X is zero for BGR24 input.
    // BGRX's unused byte is preserved. Rejects unsupported headers/compression,
    // overflow and truncation transactionally, without a graphics/device API.
    bool Read(const void *data, std::size_t bytes);
};
} // namespace KauaiPixels
#endif
