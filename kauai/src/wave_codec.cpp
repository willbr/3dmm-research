/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "wave_codec.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>
namespace KauaiAudio
{
namespace
{
constexpr int Coefficients[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};
constexpr int Adaptation[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
constexpr std::size_t BlockBytes = 256, BlockSamples = 500;
void Word(std::vector<std::uint8_t> &v, std::uint32_t n)
{
    v.push_back(n & 255);
    v.push_back((n >> 8) & 255);
}
void Dword(std::vector<std::uint8_t> &v, std::uint32_t n)
{
    Word(v, n);
    Word(v, n >> 16);
}
void Tag(std::vector<std::uint8_t> &v, const char *tag)
{
    v.insert(v.end(), tag, tag + 4);
}
unsigned U16(const std::uint8_t *p)
{
    return p[0] | (unsigned(p[1]) << 8);
}
std::uint32_t U32(const std::uint8_t *p)
{
    return U16(p) | (std::uint32_t(U16(p + 2)) << 16);
}
void Header(std::vector<std::uint8_t> &v, std::uint32_t payload, std::uint32_t rate, bool adpcm, std::uint32_t frames,
            unsigned channels = 1, unsigned bits = 8)
{
    Tag(v, "RIFF");
    Dword(v, (adpcm ? 82u : 36u) + payload + (payload & 1));
    Tag(v, "WAVE");
    Tag(v, "fmt ");
    Dword(v, adpcm ? 50 : 16);
    Word(v, adpcm ? 2 : 1);
    Word(v, channels);
    Dword(v, rate);
    Dword(v, adpcm ? static_cast<std::uint32_t>(std::uint64_t(rate) * BlockBytes / BlockSamples)
                   : rate * channels * (bits / 8));
    Word(v, adpcm ? BlockBytes : channels * (bits / 8));
    Word(v, adpcm ? 4 : bits);
    if (adpcm)
    {
        Word(v, 32);
        Word(v, BlockSamples);
        Word(v, 7);
        for (const auto &pair : Coefficients)
            for (int coefficient : pair)
                Word(v, static_cast<std::uint16_t>(coefficient));
        Tag(v, "fact");
        Dword(v, 4);
        Dword(v, frames);
    }
    Tag(v, "data");
    Dword(v, payload);
}
std::uint64_t Block(const std::array<int, BlockSamples> &samples, std::size_t valid, int predictor,
                    std::array<std::uint8_t, BlockBytes> &bytes)
{
    int older = samples[0], last = samples[1];
    int delta = std::max(16, std::min(32767, std::abs(last - older)));
    const int initialDelta = delta;
    bytes[0] = static_cast<std::uint8_t>(predictor);
    bytes[1] = initialDelta & 255;
    bytes[2] = initialDelta >> 8;
    bytes[3] = last & 255;
    bytes[4] = (static_cast<std::uint16_t>(last) >> 8);
    bytes[5] = older & 255;
    bytes[6] = (static_cast<std::uint16_t>(older) >> 8);
    std::uint64_t error = 0;
    for (std::size_t i = 2; i < BlockSamples; ++i)
    {
        const int prediction = (last * Coefficients[predictor][0] + older * Coefficients[predictor][1]) / 256;
        const int residual = samples[i] - prediction;
        int nibble = (residual + (residual < 0 ? -delta / 2 : delta / 2)) / delta;
        nibble = std::max(-8, std::min(7, nibble));
        // Keep delta in the common signed/unsigned decoder range by choosing
        // a smaller residual code when necessary, rather than changing the
        // specified decoder's adaptation arithmetic.
        while (delta * Adaptation[nibble & 15] / 256 > 32767)
            nibble += nibble < 0 ? 1 : -1;
        const int decoded = std::max(-32768, std::min(32767, prediction + nibble * delta));
        const auto difference = std::int64_t(samples[i]) - decoded;
        if (i < valid)
            error += static_cast<std::uint64_t>(difference * difference);
        older = last;
        last = decoded;
        delta = std::max(16, delta * Adaptation[nibble & 15] / 256);
        auto &byte = bytes[7 + (i - 2) / 2];
        if (!(i & 1))
            byte = static_cast<std::uint8_t>((nibble & 15) << 4);
        else
            byte |= nibble & 15;
    }
    return error;
}
} // namespace
bool Pcm8Wave(const std::vector<std::uint8_t> &samples, std::uint32_t rate, std::vector<std::uint8_t> &wave)
{
    return PcmWave(samples, rate, 1, 8, wave);
}
bool PcmWave(const std::vector<std::uint8_t> &samples, std::uint32_t rate, unsigned channels, unsigned bits,
             std::vector<std::uint8_t> &wave)
{
    if (!rate || rate > INT32_MAX || !channels || channels > 2 || (bits != 8 && bits != 16) || samples.empty() ||
        samples.size() > INT32_MAX - 45 || samples.size() % (channels * (bits / 8)) ||
        std::uint64_t(rate) * channels * (bits / 8) > UINT32_MAX)
        return false;
    try
    {
        std::vector<std::uint8_t> result;
        result.reserve(44 + samples.size() + 1);
        Header(result, static_cast<std::uint32_t>(samples.size()), rate, false,
               static_cast<std::uint32_t>(samples.size() / (channels * (bits / 8))), channels, bits);
        result.insert(result.end(), samples.begin(), samples.end());
        if (samples.size() & 1)
            result.push_back(0);
        wave.swap(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool AdpcmWave(const std::vector<std::uint8_t> &samples, std::uint32_t rate, std::vector<std::uint8_t> &wave)
{
    if (!rate || rate > INT32_MAX || samples.empty() || samples.size() > INT32_MAX ||
        std::uint64_t(rate) * BlockBytes / BlockSamples > UINT32_MAX)
        return false;
    const auto blocks = (samples.size() + BlockSamples - 1) / BlockSamples;
    if (blocks > (INT32_MAX - 90) / BlockBytes)
        return false;
    try
    {
        std::vector<std::uint8_t> result;
        result.reserve(90 + blocks * BlockBytes);
        Header(result, static_cast<std::uint32_t>(blocks * BlockBytes), rate, true,
               static_cast<std::uint32_t>(samples.size()));
        for (std::size_t block = 0; block < blocks; ++block)
        {
            std::array<int, BlockSamples> input;
            for (std::size_t i = 0; i < BlockSamples; ++i)
            {
                const auto at = std::min(block * BlockSamples + i, samples.size() - 1);
                input[i] = (int(samples[at]) - 128) * 256;
            }
            std::array<std::uint8_t, BlockBytes> best, candidate;
            auto minimum = std::numeric_limits<std::uint64_t>::max();
            for (int predictor = 0; predictor < 7; ++predictor)
            {
                const auto error =
                    Block(input, std::min(BlockSamples, samples.size() - block * BlockSamples), predictor, candidate);
                if (error < minimum)
                {
                    minimum = error;
                    best = candidate;
                }
            }
            result.insert(result.end(), best.begin(), best.end());
        }
        wave.swap(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool LegacyImportWave(const std::vector<std::uint8_t> &samples, std::vector<std::uint8_t> &wave)
{
    // AudioMan's ACM reader substitutes dataBytes*2 for decoded length when
    // fewer than four 256-byte blocks are present. Its fact chunk is ignored.
    // PCM preserves the duration of these short imports in the original Studio.
    return samples.size() <= 3 * BlockSamples ? Pcm8Wave(samples, 11025, wave) : AdpcmWave(samples, 11025, wave);
}
bool LegacyWaveFormat(const void *data, std::size_t bytes)
{
    const auto p = static_cast<const std::uint8_t *>(data);
    if (!p || bytes < 12 || std::memcmp(p, "RIFF", 4) || std::memcmp(p + 8, "WAVE", 4))
        return false;
    const auto end = std::uint64_t(U32(p + 4)) + 8;
    if (end > bytes || end < 12)
        return false;
    for (std::uint64_t at = 12; at + 8 <= end;)
    {
        const auto size = U32(p + at + 4);
        if (size > end - at - 8)
            return false;
        if (!std::memcmp(p + at, "fmt ", 4))
        {
            if (size < 16)
                return false;
            const auto format = p + at + 8;
            const auto channels = U16(format + 2), bits = U16(format + 14);
            if (!channels || channels > 2 || !U32(format + 4) || U32(format + 4) > 48000)
                return false;
            if (U16(format) == 1)
                return (bits == 8 || bits == 16) && U16(format + 12) == channels * (bits / 8);
            if (U16(format) != 2 || channels != 1 || U32(format + 4) != 11025 || U16(format + 12) != 256 || bits != 4 ||
                size < 50 || U16(format + 16) != 32 || U16(format + 18) != 500 || U16(format + 20) != 7)
                return false;
            for (int i = 0; i < 7; ++i)
                for (int j = 0; j < 2; ++j)
                    if (U16(format + 22 + i * 4 + j * 2) != static_cast<std::uint16_t>(Coefficients[i][j]))
                        return false;
            return true;
        }
        at += 8 + size + (size & 1);
    }
    return false;
}
bool WaveFactFrames(const void *data, std::size_t bytes, std::uint32_t &frames)
{
    const auto p = static_cast<const std::uint8_t *>(data);
    if (!p || bytes < 12 || std::memcmp(p, "RIFF", 4) || std::memcmp(p + 8, "WAVE", 4))
        return false;
    const auto end = std::uint64_t(U32(p + 4)) + 8;
    if (end > bytes || end < 12)
        return false;
    unsigned format = 0;
    std::uint32_t count = 0;
    for (std::uint64_t at = 12; at + 8 <= end;)
    {
        const auto length = U32(p + at + 4);
        if (length > end - at - 8)
            return false;
        if (!std::memcmp(p + at, "fmt ", 4) && length >= 16)
            format = U16(p + at + 8);
        if (!std::memcmp(p + at, "fact", 4) && length >= 4)
            count = U32(p + at + 8);
        at += 8 + length + (length & 1);
    }
    if (!count || (format != 2 && format != 0x11))
        return false;
    frames = count;
    return true;
}
} // namespace KauaiAudio
