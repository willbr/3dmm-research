/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "wave_codec.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <windows.h>
#include <mmreg.h>
#include "audioman.h"
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Original AudioMan compatibility failed: %d\n", __LINE__);                            \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
int main()
{
    for (unsigned frames : {1u, 2u, 3u, 255u, 256u, 499u, 500u, 501u, 1499u, 1500u, 1501u, 2000u, 11025u})
        for (int pattern = 0; pattern < 3; ++pattern)
        {
            std::vector<std::uint8_t> samples(frames), wave;
            for (unsigned i = 0; i < frames; ++i)
                samples[i] = pattern == 0   ? 128
                             : pattern == 1 ? 160
                                            : static_cast<std::uint8_t>(128 + 80 * std::sin(i * 0.04));
            CHECK(KauaiAudio::LegacyImportWave(samples, wave));
            LPSOUND source = nullptr, converted = nullptr;
            CHECK(SUCCEEDED(AllocSoundFromMemory(&source, wave.data(), static_cast<DWORD>(wave.size()))) && source);
            WAVEFORMATEX target = {};
            target.wFormatTag = WAVE_FORMAT_PCM;
            target.nChannels = 1;
            target.nSamplesPerSec = 11025;
            target.nAvgBytesPerSec = 22050;
            target.nBlockAlign = 2;
            target.wBitsPerSample = 16;
            CHECK(SUCCEEDED(AllocConvertFilter(&converted, source, &target)) && converted);
            CHECK(SUCCEEDED(converted->SetMode(TRUE, TRUE)));
            CHECK(converted->GetSamples() >= frames);
            DWORD count = frames;
            std::vector<std::int16_t> decoded(frames);
            CHECK(SUCCEEDED(converted->GetSampleData(reinterpret_cast<LPBYTE>(decoded.data()), 0, &count, nullptr)) &&
                  count == frames);
            CHECK(decoded[0] == (int(samples[0]) - 128) * 256 &&
                  (frames == 1 || decoded[1] == (int(samples[1]) - 128) * 256));
            if (pattern < 2)
                for (auto value : decoded)
                    CHECK(value == (pattern ? 8192 : 0));
            CHECK(SUCCEEDED(converted->SetMode(FALSE, TRUE)));
            converted->Release();
            source->Release();
        }
    std::puts("Original 1995 x86 AudioMan: 39 normalized imports loaded, converted and read successfully, including "
              "short PCM clips and partial final ADPCM blocks.");
}
