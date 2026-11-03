/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_audio.h"
#include "wave_codec.h"
#include "wave_fixture.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <mmreg.h>
#include <msacm.h>
#endif
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL WAVE import failed: %d\n", __LINE__);                                            \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static std::uint32_t Dword(const std::vector<std::uint8_t> &v, unsigned at)
{
    return v[at] | (std::uint32_t(v[at + 1]) << 8) | (std::uint32_t(v[at + 2]) << 16) |
           (std::uint32_t(v[at + 3]) << 24);
}
static void Compatible(const std::vector<std::uint8_t> &wave, std::size_t frames)
{
    SDL_AudioSpec spec;
    Uint8 *pcm = nullptr;
    Uint32 bytes = 0;
    CHECK(SDL_LoadWAV_IO(SDL_IOFromConstMem(wave.data(), wave.size()), true, &spec, &pcm, &bytes));
    CHECK(spec.format == SDL_AUDIO_S16LE && spec.channels == 1 && spec.freq == 11025 && bytes >= frames * 2);
    const auto actualBytes = static_cast<unsigned>(frames * 2);
#ifdef _WIN32
    WAVEFORMATEX destination = {};
    destination.wFormatTag = WAVE_FORMAT_PCM;
    destination.nChannels = 1;
    destination.nSamplesPerSec = 11025;
    destination.nAvgBytesPerSec = 22050;
    destination.nBlockAlign = 2;
    destination.wBitsPerSample = 16;
    std::vector<std::uint8_t> format(wave.begin() + 20, wave.begin() + 70);
    HACMSTREAM stream = nullptr;
    CHECK(!acmStreamOpen(&stream, nullptr, reinterpret_cast<LPWAVEFORMATEX>(format.data()), &destination, nullptr, 0, 0,
                         ACM_STREAMOPENF_NONREALTIME));
    DWORD required = 0;
    CHECK(!acmStreamSize(stream, Dword(wave, 86), &required, ACM_STREAMSIZEF_SOURCE));
    std::vector<std::uint8_t> decoded(required);
    ACMSTREAMHEADER header = {};
    header.cbStruct = sizeof(header);
    header.pbSrc = const_cast<LPBYTE>(wave.data() + 90);
    header.cbSrcLength = Dword(wave, 86);
    header.pbDst = decoded.data();
    header.cbDstLength = required;
    CHECK(!acmStreamPrepareHeader(stream, &header, 0));
    CHECK(!acmStreamConvert(stream, &header,
                            ACM_STREAMCONVERTF_START | ACM_STREAMCONVERTF_END | ACM_STREAMCONVERTF_BLOCKALIGN));
    CHECK(header.cbDstLengthUsed >= actualBytes && !std::memcmp(decoded.data(), pcm, actualBytes));
    CHECK(!acmStreamUnprepareHeader(stream, &header, 0) && !acmStreamClose(stream, 0));
#endif
    SDL_free(pcm);
}
int main()
{
    std::uint32_t random = 2391;
    for (unsigned count : {1u, 2u, 3u, 499u, 500u, 501u, 11025u})
        for (unsigned pattern = 0; pattern < 4; ++pattern)
        {
            std::vector<std::uint8_t> samples(count), wave;
            for (unsigned i = 0; i < count; ++i)
            {
                random = random * 1664525u + 1013904223u;
                samples[i] = pattern == 0   ? 128
                             : pattern == 1 ? static_cast<std::uint8_t>(128 + 100 * std::sin(i * 0.04))
                             : pattern == 2 ? static_cast<std::uint8_t>((i & 1) ? 255 : 0)
                                            : static_cast<std::uint8_t>(random >> 24);
            }
            CHECK(KauaiAudio::AdpcmWave(samples, 11025, wave));
            Compatible(wave, count);
        }
    const auto shortWave = WaveFixture::Pcm({-32768, 0, 32767});
    std::vector<std::uint8_t> imported;
    CHECK(KauaiAudio::PrepareWaveImport(shortWave.data(), shortWave.size(), imported) && imported == shortWave);
    for (unsigned count : {499u, 500u, 501u, 1499u, 1500u, 1501u})
    {
        const auto input = WaveFixture::Pcm8(std::vector<std::uint8_t>(count, 160), 11025);
        CHECK(KauaiAudio::PrepareWaveImport(input.data(), input.size(), imported));
        CHECK(imported[20] == (count <= 1500 ? 1 : 2));
        if (count <= 1500)
            CHECK(imported == input);
        else
            CHECK(Dword(imported, 78) == count);
    }
    const auto original = WaveFixture::Pcm(std::vector<std::int16_t>(48000, 8192));
    CHECK(KauaiAudio::PrepareWaveImport(original.data(), original.size(), imported));
    CHECK(imported.size() < original.size() && imported[20] == 2 && Dword(imported, 24) == 11025 &&
          Dword(imported, 78) == 11025);
    std::vector<std::uint8_t> repeated;
    CHECK(KauaiAudio::PrepareWaveImport(original.data(), original.size(), repeated) && repeated == imported);
    Compatible(imported, 11025);
    std::shared_ptr<const KauaiAudio::Sample> playback;
    CHECK(KauaiAudio::DecodeWave(imported.data(), imported.size(), playback) && playback->stereo.size() == 96000);
    std::vector<std::uint8_t> retained;
    CHECK(KauaiAudio::PrepareWaveImport(imported.data(), imported.size(), retained) && retained == imported);
    const auto saved = imported;
    CHECK(!KauaiAudio::PrepareWaveImport("bad", 3, imported) && imported == saved);
    // Modern float/stereo input must produce a format the old Studio reads,
    // even when a full ADPCM block would be larger than the short result.
    std::vector<std::uint8_t> format, data;
    WaveFixture::Word(format, 3);
    WaveFixture::Word(format, 2);
    WaveFixture::Dword(format, 48000);
    WaveFixture::Dword(format, 384000);
    WaveFixture::Word(format, 8);
    WaveFixture::Word(format, 32);
    for (int frame = 0; frame < 480; ++frame)
        for (float value : {0.75f, 0.25f})
        {
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            WaveFixture::Dword(data, bits);
        }
    const auto modern = WaveFixture::Wave(format, data, 480);
    CHECK(KauaiAudio::PrepareWaveImport(modern.data(), modern.size(), imported));
    CHECK(imported[20] == 1 && imported[22] == 1 && imported[34] == 8 && Dword(imported, 24) == 11025);
    CHECK(KauaiAudio::LegacyWaveFormat(imported.data(), imported.size()));
    SDL_AudioSpec spec;
    Uint8 *normalized = nullptr;
    Uint32 count = 0;
    CHECK(SDL_LoadWAV_IO(SDL_IOFromConstMem(imported.data(), imported.size()), true, &spec, &normalized, &count));
    CHECK(spec.format == SDL_AUDIO_U8 && count > 0 && normalized[count / 2] >= 191 && normalized[count / 2] <= 193);
    SDL_free(normalized);
    std::puts("WAVE import: 28 encoder streams decode identically in SDL/Windows ACM, partial-block fact counts, "
              "resampling/compression, source preservation and rejection passed.");
}
