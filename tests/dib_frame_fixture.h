/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef DIB_FRAME_FIXTURE_H
#define DIB_FRAME_FIXTURE_H
#include <cstdint>
#include <vector>
inline void PutDib32(std::vector<std::uint8_t> &data, unsigned offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        data[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
inline std::vector<std::uint8_t> MakeDib(int width, int height, int depth, bool bottomUp)
{
    const int pitch = ((width * depth + 31) / 32) * 4;
    std::vector<std::uint8_t> result(40 + pitch * height, 0xCD);
    for (int i = 0; i < 40; ++i)
        result[i] = 0;
    PutDib32(result, 0, 40);
    PutDib32(result, 4, width);
    PutDib32(result, 8, bottomUp ? height : -height);
    result[12] = 1;
    result[14] = static_cast<std::uint8_t>(depth);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto offset = 40 + (bottomUp ? height - y - 1 : y) * pitch + x * (depth / 8);
            result[offset] = static_cast<std::uint8_t>(x * 13 + y * 7);
            result[offset + 1] = static_cast<std::uint8_t>(x * 31 + y * 3);
            result[offset + 2] = static_cast<std::uint8_t>(x * 5 + y * 19);
            if (depth == 32)
                result[offset + 3] = 0x91;
        }
    return result;
}
#endif
