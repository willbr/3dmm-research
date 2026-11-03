/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef TEST_AVI_FIXTURE_H
#define TEST_AVI_FIXTURE_H
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
namespace AviFixture
{
using Bytes = std::vector<std::uint8_t>;
static void Put32(Bytes &b, std::size_t p, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b[p + i] = static_cast<std::uint8_t>(v >> (i * 8));
}
static Bytes Chunk(const char *tag, Bytes data)
{
    Bytes b(tag, tag + 4);
    b.resize(8);
    Put32(b, 4, static_cast<std::uint32_t>(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    if (b.size() & 1)
        b.push_back(0);
    return b;
}
static void Add(Bytes &b, const Bytes &v)
{
    b.insert(b.end(), v.begin(), v.end());
}
static std::size_t Payload(const Bytes &b, const char *tag)
{
    return static_cast<std::size_t>(std::search(b.begin(), b.end(), tag, tag + 4) - b.begin()) + 8;
}
static Bytes List(const char *tag, const char *type, const Bytes &data)
{
    Bytes b(type, type + 4);
    Add(b, data);
    return Chunk(tag, b);
}
static Bytes Fixture(bool playback = false, bool sixteen = false)
{
    Bytes sh(56), sf(40 + 1024);
    std::memcpy(sh.data(), "vidsmsvc", 8);
    Put32(sh, 20, 166667);
    Put32(sh, 24, 1000000);
    Put32(sh, 28, playback ? 0 : 3);
    Put32(sh, 32, 3);
    Put32(sf, 0, 40);
    Put32(sf, 4, 4);
    Put32(sf, 8, 4);
    sf[12] = 1;
    sf[14] = 8;
    std::memcpy(sf.data() + 16, "CRAM", 4);
    for (unsigned i = 0; i < 256; ++i)
        Put32(sf, 40 + i * 4, i | ((255 - i) << 8) | (i << 16));
    Bytes video = Chunk("strh", sh);
    Add(video, Chunk("strf", sf));
    std::fill(sh.begin(), sh.end(), 0);
    std::memcpy(sh.data(), "auds", 4);
    const unsigned align = sixteen ? 2 : 1;
    Put32(sh, 20, align);
    Put32(sh, 24, 22050 * align);
    Put32(sh, 32, playback ? 11025 : 4);
    Put32(sh, 44, align);
    sf.assign(16, 0);
    sf[0] = 1;
    sf[2] = 1;
    Put32(sf, 4, 22050);
    Put32(sf, 8, 22050 * align);
    sf[12] = static_cast<std::uint8_t>(align);
    sf[14] = sixteen ? 16 : 8;
    Bytes audio = Chunk("strh", sh);
    Add(audio, Chunk("strf", sf));
    Bytes header = List("LIST", "strl", video);
    Add(header, List("LIST", "strl", audio));
    Bytes packets = Chunk("00dc", {7, 0x80, 0, 0});
    Bytes pcm{128, 129, 130, 131};
    if (playback)
    {
        pcm.resize(11025 * align);
        for (unsigned i = 0; i < 11025; ++i)
            if (sixteen)
            {
                const auto value = static_cast<std::uint16_t>(i % 100 < 50 ? 8192 : -8192);
                pcm[i * 2] = static_cast<std::uint8_t>(value);
                pcm[i * 2 + 1] = static_cast<std::uint8_t>(value >> 8);
            }
            else
                pcm[i] = i % 100 < 50 ? 156 : 100;
    }
    const auto middle = pcm.size() / 2 / align * align;
    Add(packets, Chunk("01wb", Bytes(pcm.begin(), pcm.begin() + middle)));
    Add(packets, Chunk("00pc", {7, 1, 0, 0, 11, 22, 33, 0}));
    Add(packets, Chunk("00dc", {1, 0x84, 0, 0}));
    Add(packets, Chunk("01wb", Bytes(pcm.begin() + middle, pcm.end())));
    Add(packets, Chunk("00dc", {}));
    Bytes body = List("LIST", "hdrl", header);
    Add(body, Chunk("JUNK", {1, 2, 3}));
    Add(body, List("LIST", "movi", List("LIST", "rec ", packets)));
    return List("RIFF", "AVI ", body);
}
} // namespace AviFixture
#endif
