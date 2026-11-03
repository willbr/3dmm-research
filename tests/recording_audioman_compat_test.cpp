/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "recording_data.h"
#include "wave_codec.h"
#include <windows.h>
#include <mmreg.h>
#include "audioman.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Recording AudioMan compatibility failed: %d\n", __LINE__);                           \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
int main()
{
    for (unsigned bits : {8u, 16u})
        for (unsigned count : {9u, 100u, 500u, 11033u, 22058u})
            for (unsigned pattern = 0; pattern < 5; ++pattern)
            {
                std::vector<std::uint8_t> pcm;
                for (unsigned i = 0; i < count; ++i)
                {
                    int value = pattern == 0 ? 0 : pattern == 1 ? 32 : pattern == 4 ? 2 : 20;
                    if (pattern == 2 && i >= 4000 && i < 5000)
                        value += (i & 1) ? 50 : -50;
                    if (pattern == 3 && i >= 7000 && i < 8000)
                        value += (i & 1) ? 50 : -50;
                    if (bits == 8)
                        pcm.push_back(static_cast<std::uint8_t>(value + 128));
                    else
                    {
                        const auto word = static_cast<std::uint16_t>(value * 256);
                        pcm.push_back(word & 255);
                        pcm.push_back(word >> 8);
                    }
                }
                KauaiAudio::RecordingBuffer capture;
                std::vector<std::uint8_t> actual, source;
                CHECK(capture.Init(11025, 1, bits, 3000));
                CHECK(capture.Append(pcm.data(), pcm.size()) == pcm.size() && capture.Wave(actual));
                pcm.resize(pcm.size() - 8 * (bits / 8));
                CHECK(KauaiAudio::PcmWave(pcm, 11025, 1, bits, source));
                LPSOUND sound = nullptr, bias = nullptr, trim = nullptr;
                CHECK(SUCCEEDED(AllocSoundFromMemory(&sound, source.data(), static_cast<DWORD>(source.size()))));
                CHECK(SUCCEEDED(AllocBiasFilter(&bias, sound)));
                CHECK(SUCCEEDED(AllocTrimFilter(&trim, bias)));
                CHECK(SUCCEEDED(trim->SetMode(TRUE, TRUE)));
                DWORD frames = trim->GetSamples();
                std::vector<std::uint8_t> expected(frames * (bits / 8));
                DWORD got = frames;
                CHECK(SUCCEEDED(trim->GetSampleData(expected.data(), 0, &got, nullptr)) && got == frames);
                const unsigned actualBytes = actual[40] | (unsigned(actual[41]) << 8) | (unsigned(actual[42]) << 16) |
                                             (unsigned(actual[43]) << 24);
                if (expected.size() != actualBytes || std::memcmp(expected.data(), actual.data() + 44, actualBytes))
                {
                    std::fprintf(stderr, "bits %u count %u pattern %u: native bytes %zu, portable %u\n", bits, count,
                                 pattern, expected.size(), actualBytes);
                    return 1;
                }
                CHECK(SUCCEEDED(trim->SetMode(FALSE, TRUE)));
                trim->Release();
                bias->Release();
                sound->Release();
            }
    std::puts("Recording processing: 50 mono PCM8/16 captures match the original 1995 AudioMan bias/trim filters byte "
              "for byte.");
}
