/* Copyright (c) 3DMMForever contributors. Licensed under the MIT License. */
#include "mcp_io.h"
#include <algorithm>

namespace mcp
{
namespace
{
void BigEndian(std::vector<unsigned char> &out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<unsigned char>(value >> shift));
}
void Chunk(std::vector<unsigned char> &out, const char *type, const std::vector<unsigned char> &data)
{
    BigEndian(out, static_cast<std::uint32_t>(data.size()));
    const auto start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    std::uint32_t crc = 0xffffffff;
    for (auto i = start; i < out.size(); ++i)
    {
        crc ^= out[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    BigEndian(out, ~crc);
}
} // namespace

std::vector<unsigned char> EncodePng(const std::vector<std::uint32_t> &pixels, int width, int height)
{
    // Debug screenshots have bounded canvas sizes. Reject invalid dimensions
    // before multiplication/allocation, including malformed framebuffer input.
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        return {};
    const auto row = static_cast<std::size_t>(width) * 3 + 1;
    const auto count = static_cast<std::size_t>(width) * height;
    if (pixels.size() != count || row * height > 256 * 1024 * 1024)
        return {};
    std::vector<unsigned char> raw;
    raw.reserve(row * height);
    std::uint32_t a = 1, b = 0;
    auto append = [&](unsigned char byte) {
        raw.push_back(byte);
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    };
    for (int y = 0; y < height; ++y)
    {
        append(0); // PNG filter: None
        for (int x = 0; x < width; ++x)
        {
            const auto pixel = pixels[static_cast<std::size_t>(y) * width + x];
            append(static_cast<unsigned char>(pixel >> 16));
            append(static_cast<unsigned char>(pixel >> 8));
            append(static_cast<unsigned char>(pixel));
        }
    }
    // RFC 1950 zlib stream containing RFC 1951 stored DEFLATE blocks. No
    // compression dependency; PNG stays lossless and works on every backend.
    std::vector<unsigned char> compressed{0x78, 0x01};
    for (std::size_t offset = 0; offset < raw.size();)
    {
        const auto length = std::min<std::size_t>(65535, raw.size() - offset);
        const auto inverse = static_cast<std::uint16_t>(~length);
        compressed.push_back(offset + length == raw.size() ? 1 : 0);
        compressed.push_back(static_cast<unsigned char>(length));
        compressed.push_back(static_cast<unsigned char>(length >> 8));
        compressed.push_back(static_cast<unsigned char>(inverse));
        compressed.push_back(static_cast<unsigned char>(inverse >> 8));
        compressed.insert(compressed.end(), raw.begin() + offset, raw.begin() + offset + length);
        offset += length;
    }
    BigEndian(compressed, (b << 16) | a);
    std::vector<unsigned char> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<unsigned char> header;
    BigEndian(header, static_cast<std::uint32_t>(width));
    BigEndian(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, 2, 0, 0, 0});
    Chunk(png, "IHDR", header);
    Chunk(png, "IDAT", compressed);
    Chunk(png, "IEND", {});
    return png;
}
} // namespace mcp
