/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "wave_codec.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WINDOWS_
#error Wave encoding must not import the Windows SDK
#endif
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Wave codec failed: %d\n", __LINE__);                                                 \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static std::uint32_t Dword(const std::vector<std::uint8_t> &v, unsigned at)
{
    return v[at] | (std::uint32_t(v[at + 1]) << 8) | (std::uint32_t(v[at + 2]) << 16) |
           (std::uint32_t(v[at + 3]) << 24);
}
int main()
{
    std::vector<std::uint8_t> result, repeat;
    CHECK(KauaiAudio::Pcm8Wave({1, 2, 3}, 11025, result));
    CHECK(result.size() == 48 && Dword(result, 4) == 40 && Dword(result, 16) == 16 && Dword(result, 40) == 3);
    CHECK(result[44] == 1 && result[45] == 2 && result[46] == 3 && !result[47]);
    CHECK(KauaiAudio::LegacyWaveFormat(result.data(), result.size()));
    for (unsigned count : {1u, 2u, 3u, 499u, 500u, 501u, 1000u, 11025u})
    {
        std::vector<std::uint8_t> silence(count, 128);
        CHECK(KauaiAudio::AdpcmWave(silence, 11025, result));
        const auto blocks = (count + 499) / 500;
        CHECK(result.size() == 90 + blocks * 256 && Dword(result, 4) == result.size() - 8);
        CHECK(!std::memcmp(result.data() + 12, "fmt ", 4) && Dword(result, 16) == 50 && result[20] == 2 &&
              result[22] == 1);
        CHECK(Dword(result, 24) == 11025 && result[32] == 0 && result[33] == 1 && result[34] == 4);
        CHECK(result[36] == 32 && result[38] == 244 && result[39] == 1 && result[40] == 7);
        CHECK(!std::memcmp(result.data() + 70, "fact", 4) && Dword(result, 78) == count &&
              Dword(result, 86) == blocks * 256);
        for (unsigned block = 0; block < blocks; ++block)
            for (unsigned i = 0; i < 256; ++i)
                CHECK(result[90 + block * 256 + i] == (i == 1 ? 16 : 0));
        CHECK(KauaiAudio::LegacyWaveFormat(result.data(), result.size()));
        std::uint32_t fact = 0;
        CHECK(KauaiAudio::WaveFactFrames(result.data(), result.size(), fact) && fact == count);
        CHECK(KauaiAudio::AdpcmWave(silence, 11025, repeat) && repeat == result);
    }
    for (unsigned count : {1u, 499u, 500u, 501u, 1499u, 1500u, 1501u, 2000u, 11025u})
    {
        CHECK(KauaiAudio::LegacyImportWave(std::vector<std::uint8_t>(count, 128), result));
        CHECK(result[20] == (count <= 1500 ? 1 : 2));
    }
    const auto saved = result;
    std::uint32_t frames = 42;
    for (unsigned i = 78; i < 82; ++i)
        result[i] = 0;
    CHECK(!KauaiAudio::WaveFactFrames(result.data(), result.size(), frames) && frames == 42);
    result = saved;
    CHECK(!KauaiAudio::AdpcmWave({}, 11025, result) && !KauaiAudio::AdpcmWave({128}, 0, result));
    CHECK(!KauaiAudio::Pcm8Wave({128}, UINT32_MAX, result) && result == saved);
    CHECK(!KauaiAudio::LegacyWaveFormat("bad", 3));
    result[4] = result[5] = result[6] = result[7] = 255;
    CHECK(!KauaiAudio::LegacyWaveFormat(result.data(), result.size()));
    std::puts("Portable WAVE codec: exact PCM/ADPCM headers, coefficients, silent blocks, partial blocks, sample "
              "counts, repeat encoding and rejected inputs passed.");
}
