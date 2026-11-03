/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "avi_movie.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <utility>
// Format references (independent implementation):
// https://multimedia.cx/video1.txt (Mike Melanson's Video 1 format description)
// https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference
namespace KauaiVideo
{
namespace
{
using Byte = std::uint8_t;
std::uint16_t U16(const Byte *p)
{
    return p[0] | (std::uint16_t(p[1]) << 8);
}
std::uint32_t U32(const Byte *p)
{
    return U16(p) | (std::uint32_t(U16(p + 2)) << 16);
}
bool Tag(const Byte *p, const char *tag)
{
    return std::memcmp(p, tag, 4) == 0;
}
bool Video1(const Byte *p)
{
    char tag[4];
    for (int i = 0; i < 4; ++i)
        tag[i] = char(p[i] >= 'a' && p[i] <= 'z' ? p[i] - 32 : p[i]);
    return Tag(reinterpret_cast<const Byte *>(tag), "MSVC") || Tag(reinterpret_cast<const Byte *>(tag), "CRAM") ||
           Tag(reinterpret_cast<const Byte *>(tag), "WHAM");
}
bool Dimensions(std::int32_t w, std::int32_t h)
{
    return w > 0 && h > 0 && w % 4 == 0 && h % 4 == 0 && std::int64_t(w) * h <= 16 * 1024 * 1024;
}
struct Chunk
{
    const Byte *tag = nullptr;
    std::size_t offset = 0, bytes = 0;
};
// Every chunk must fit its enclosing list, including the WORD padding.
template <class F> bool Chunks(const std::vector<Byte> &b, std::size_t first, std::size_t end, F visit)
{
    while (first < end)
    {
        if (end - first < 8)
            return false;
        Chunk c{b.data() + first, first + 8, U32(b.data() + first + 4)};
        if (c.bytes > end - c.offset)
            return false; // Check before padding can wrap on a 32-bit host.
        const auto padded = c.bytes + (c.bytes & 1);
        if (padded > end - c.offset || !visit(c))
            return false;
        first = c.offset + padded;
    }
    return true;
}
bool Decode(const Byte *p, std::size_t bytes, std::int32_t w, std::int32_t h, std::vector<Byte> &pixels)
{
    const std::size_t blocks = std::size_t(w / 4) * (h / 4);
    std::size_t block = 0, pos = 0;
    while (block < blocks)
    {
        if (bytes - pos < 2)
            return false;
        const auto a = p[pos++], b = p[pos++];
        if (b >= 0x84 && b < 0x88)
        {
            const auto skip = std::size_t(b - 0x84) * 256 + a;
            if (!skip || skip > blocks - block)
                return false;
            block += skip;
            continue;
        }
        const unsigned colors = b < 0x80 ? 2 : b >= 0x90 ? 8 : 0;
        if (colors > bytes - pos)
            return false;
        const auto flags = unsigned(a) | (unsigned(b) << 8);
        const auto left = std::int32_t(block % (w / 4)) * 4;
        const auto bottom = h - 1 - std::int32_t(block / (w / 4)) * 4;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                const auto pair = colors == 8 ? (y / 2 * 2 + x / 2) * 2 : 0;
                const auto index = colors ? p[pos + pair + ((flags >> (y * 4 + x)) & 1 ? 0 : 1)] : a;
                pixels[std::size_t(bottom - y) * w + left + x] = index;
            }
        pos += colors;
        ++block;
    }
    // Encoders may omit the final 0000 marker; no other trailing data is valid.
    return pos == bytes || (bytes - pos == 2 && p[pos] == 0 && p[pos + 1] == 0);
}
} // namespace
bool DecodeVideo1(const void *data, std::size_t bytes, std::int32_t w, std::int32_t h, std::vector<Byte> &pixels)
{
    if (!data || !Dimensions(w, h) || pixels.size() != std::size_t(w) * h)
        return false;
    try
    {
        auto next = pixels;
        if (!Decode(static_cast<const Byte *>(data), bytes, w, h, next))
            return false;
        pixels.swap(next);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
struct AviMovie::Data
{
    struct Frame
    {
        std::size_t offset, bytes;
        std::array<std::uint32_t, 256> palette;
    };
    struct Stream
    {
        Chunk header, format;
    };
    AviInfo info;
    AviAudio audio;
    std::vector<Byte> bytes, pixels;
    std::vector<Frame> frames;
    std::int32_t decoded = -1;
    std::uint32_t videoStream = UINT32_MAX, audioStream = UINT32_MAX;
    std::array<std::uint32_t, 256> palette{};
    bool Headers(const Chunk &list)
    {
        std::vector<Stream> streams;
        if (!Chunks(bytes, list.offset + 4, list.offset + list.bytes, [&](const Chunk &c) {
                if (!Tag(c.tag, "LIST"))
                    return true;
                if (c.bytes < 4)
                    return false;
                if (!Tag(bytes.data() + c.offset, "strl"))
                    return true;
                if (streams.size() >= 100)
                    return false;
                Stream stream;
                if (!Chunks(bytes, c.offset + 4, c.offset + c.bytes,
                            [&](const Chunk &s) {
                                if (Tag(s.tag, "strh"))
                                {
                                    if (stream.header.tag)
                                        return false;
                                    stream.header = s;
                                }
                                if (Tag(s.tag, "strf"))
                                {
                                    if (stream.format.tag)
                                        return false;
                                    stream.format = s;
                                }
                                return true;
                            }) ||
                    !stream.header.tag || stream.header.bytes < 48 || !stream.format.tag)
                    return false;
                streams.push_back(stream);
                return true;
            }))
            return false;
        for (std::size_t i = 0; i < streams.size(); ++i)
        {
            const auto &s = streams[i];
            const auto sh = bytes.data() + s.header.offset, sf = bytes.data() + s.format.offset;
            if (Tag(sh, "vids"))
            {
                if (videoStream != UINT32_MAX || s.format.bytes < 40 || U32(sf) != 40 || !Video1(sf + 16) ||
                    U16(sf + 12) != 1 || U16(sf + 14) != 8)
                    return false;
                videoStream = static_cast<std::uint32_t>(i);
                info = {static_cast<std::int32_t>(U32(sf + 4)),
                        static_cast<std::int32_t>(U32(sf + 8)),
                        static_cast<std::int32_t>(U32(sh + 32)),
                        U32(sh + 20),
                        U32(sh + 24),
                        U32(sh + 28)};
                if (!Dimensions(info.width, info.height) || info.frames <= 0 || !info.scale || !info.rate ||
                    info.frames > 256 * 1024 || info.start > INT32_MAX ||
                    std::uint64_t(info.start) + info.frames > INT32_MAX)
                    return false;
                const auto samples = std::uint64_t(info.start) + info.frames;
                if (samples * info.scale > std::uint64_t(INT32_MAX) * info.rate / 1000)
                    return false;
                const auto count = U32(sf + 32) ? U32(sf + 32) : 256;
                if (count > 256 || s.format.bytes < 40 + count * 4)
                    return false;
                for (unsigned entry = 0; entry < count; ++entry)
                    palette[entry] = U32(sf + 40 + entry * 4) & 0xFFFFFF;
            }
            else if (Tag(sh, "auds"))
            {
                if (audioStream != UINT32_MAX || s.format.bytes < 16 || U16(sf) != 1 ||
                    (U16(sf + 2) != 1 && U16(sf + 2) != 2) || (U16(sf + 14) != 8 && U16(sf + 14) != 16) ||
                    !U32(sf + 4) || U32(sf + 4) > 384000)
                    return false;
                audioStream = static_cast<std::uint32_t>(i);
                audio.channels = U16(sf + 2);
                audio.bits = U16(sf + 14);
                audio.rate = U32(sf + 4);
                const auto align = audio.channels * (audio.bits / 8);
                if (U16(sf + 12) != align || U32(sf + 8) != audio.rate * align || U32(sh + 20) != align ||
                    U32(sh + 24) != audio.rate * align || U32(sh + 44) != align)
                    return false;
                audio.start = U32(sh + 28); // PCM sample frame, not byte offset.
            }
        }
        return videoStream != UINT32_MAX;
    }
    bool Movie(std::size_t first, std::size_t end, unsigned depth = 0)
    {
        if (depth > 8)
            return false;
        return Chunks(bytes, first, end, [&](const Chunk &c) {
            if (Tag(c.tag, "LIST"))
                return c.bytes >= 4 && Tag(bytes.data() + c.offset, "rec ") &&
                       Movie(c.offset + 4, c.offset + c.bytes, depth + 1);
            if (c.tag[0] < '0' || c.tag[0] > '9' || c.tag[1] < '0' || c.tag[1] > '9')
                return true;
            const unsigned stream = (c.tag[0] - '0') * 10 + c.tag[1] - '0';
            if (stream == videoStream)
            {
                if (c.tag[2] == 'p' && c.tag[3] == 'c')
                {
                    const auto p = bytes.data() + c.offset;
                    if (c.bytes < 4)
                        return false;
                    const unsigned count = p[1] ? p[1] : 256;
                    if (unsigned(p[0]) + count > 256 || c.bytes != 4 + count * 4)
                        return false;
                    for (unsigned i = 0; i < count; ++i)
                    {
                        const auto e = p + 4 + i * 4; // PALETTEENTRY is RGB, unlike strf's BGR.
                        palette[p[0] + i] = (std::uint32_t(e[0]) << 16) | (std::uint32_t(e[1]) << 8) | e[2];
                    }
                }
                else if (c.tag[2] == 'd' && c.tag[3] == 'c')
                {
                    if (frames.size() >= static_cast<std::size_t>(info.frames))
                        return false;
                    frames.push_back({c.offset, c.bytes, palette});
                }
                else
                    return false;
            }
            else if (stream == audioStream)
            {
                if (c.tag[2] != 'w' || c.tag[3] != 'b' || c.bytes % (audio.channels * (audio.bits / 8)))
                    return false;
                audio.pcm.insert(audio.pcm.end(), bytes.begin() + c.offset, bytes.begin() + c.offset + c.bytes);
            }
            return true;
        });
    }
    bool Read(const void *source, std::size_t count)
    {
        if (!source || count < 12 || count > 256 * 1024 * 1024)
            return false;
        const auto p = static_cast<const Byte *>(source);
        if (!Tag(p, "RIFF") || !Tag(p + 8, "AVI ") || U32(p + 4) != count - 8)
            return false;
        bytes.assign(p, p + count);
        Chunk headers, movie;
        if (!Chunks(bytes, 12, count,
                    [&](const Chunk &c) {
                        if (!Tag(c.tag, "LIST"))
                            return true;
                        if (c.bytes < 4)
                            return false;
                        if (Tag(bytes.data() + c.offset, "hdrl"))
                        {
                            if (headers.tag || movie.tag)
                                return false;
                            headers = c;
                        }
                        if (Tag(bytes.data() + c.offset, "movi"))
                        {
                            if (movie.tag || !headers.tag)
                                return false;
                            movie = c;
                        }
                        return true;
                    }) ||
            !headers.tag || !movie.tag || !Headers(headers) || !Movie(movie.offset + 4, movie.offset + movie.bytes) ||
            frames.size() != static_cast<std::size_t>(info.frames))
            return false;
        pixels.resize(std::size_t(info.width) * info.height);
        return true;
    }
};
AviMovie::AviMovie() = default;
AviMovie::~AviMovie() = default;
void AviMovie::Reset()
{
    data.reset();
}
bool AviMovie::Read(const void *p, std::size_t bytes)
{
    try
    {
        auto next = std::make_unique<Data>();
        if (!next->Read(p, bytes))
            return false;
        data.swap(next);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
AviInfo AviMovie::Info() const
{
    return data ? data->info : AviInfo{};
}
const AviAudio &AviMovie::Audio() const
{
    static const AviAudio empty;
    return data ? data->audio : empty;
}
std::int32_t AviMovie::TimeForFrame(std::int32_t frame) const
{
    if (!data || frame < 0 || frame > data->info.frames)
        return -1;
    const auto &i = data->info;
    return static_cast<std::int32_t>((std::uint64_t(frame) + i.start) * i.scale * 1000 / i.rate);
}
std::int32_t AviMovie::FrameForTime(std::int32_t milliseconds) const
{
    if (!data)
        return -1;
    const auto &i = data->info;
    if (milliseconds < 0)
        return -1 - static_cast<std::int32_t>(i.start);
    const auto frame = std::int64_t(milliseconds) * i.rate / (std::int64_t(i.scale) * 1000) - i.start;
    return static_cast<std::int32_t>((std::min)(frame, std::int64_t(i.frames)));
}
bool AviMovie::DecodeFrame(std::int32_t frame, KauaiPixels::DibFrame &output)
{
    if (!data || frame < 0 || frame >= data->info.frames)
        return false;
    try
    {
        auto &d = *data;
        if (frame < d.decoded)
        {
            std::fill(d.pixels.begin(), d.pixels.end(), 0);
            d.decoded = -1;
        }
        while (d.decoded < frame)
        {
            const auto &packet = d.frames[d.decoded + 1];
            // Empty packets repeat the preceding frame.
            if (packet.bytes &&
                !DecodeVideo1(d.bytes.data() + packet.offset, packet.bytes, d.info.width, d.info.height, d.pixels))
                return false;
            ++d.decoded;
        }
        KauaiPixels::DibFrame next;
        next.width = d.info.width;
        next.height = d.info.height;
        next.pitch = next.width * 4;
        next.sourceDepth = 32; // Palette-expanded BGRX, ready for the portable frame drawing API.
        next.pixels.resize(d.pixels.size() * 4);
        for (std::size_t i = 0; i < d.pixels.size(); ++i)
        {
            const auto color = d.frames[frame].palette[d.pixels[i]];
            next.pixels[i * 4] = Byte(color);
            next.pixels[i * 4 + 1] = Byte(color >> 8);
            next.pixels[i * 4 + 2] = Byte(color >> 16);
            next.pixels[i * 4 + 3] = 0;
        }
        output = std::move(next);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
} // namespace KauaiVideo
