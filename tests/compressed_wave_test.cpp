/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "compressed_wave.h"
#ifndef KAUAI_TEST_CODEC_CORE_ONLY
#include "sdl_audio.h"
#endif
#include "wave_fixture.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Compressed WAVE failed: %d\n", __LINE__);                                            \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static std::vector<std::uint8_t> Read(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static unsigned Word(const std::uint8_t *p)
{
    return p[0] | (unsigned(p[1]) << 8);
}
static void Compare(const std::string &folder, const char *name, int channels, int rate, int tolerance)
{
    auto wave = Read(folder + "/" + name + ".wav");
    auto reference = Read(folder + "/" + name + "-reference.s16");
    KauaiAudio::DecodedWave decoded;
    CHECK(KauaiAudio::DecodeCompressedWave(wave.data(), wave.size(), decoded) ==
          KauaiAudio::CompressedWaveResult::Decoded);
    std::printf("%s decoded %zu samples, reference %zu\n", name, decoded.samples.size(), reference.size() / 2);
    CHECK(decoded.channels == channels && decoded.sampleRate == rate && decoded.samples.size() * 2 == reference.size());
    for (std::size_t i = 0; i < decoded.samples.size(); ++i)
        CHECK(std::abs(int(decoded.samples[i]) - int(static_cast<std::int16_t>(Word(reference.data() + 2 * i)))) <=
              tolerance);
    auto retained = decoded.samples;
    for (std::size_t length = 0; length < wave.size(); ++length)
    {
        CHECK(KauaiAudio::DecodeCompressedWave(wave.data(), length, decoded) !=
              KauaiAudio::CompressedWaveResult::Decoded);
        CHECK(decoded.samples == retained); // Failure cannot replace a caller's sound.
    }
    // Playback and importing both use this decoder, then SDL's resampler.
#ifndef KAUAI_TEST_CODEC_CORE_ONLY
    std::shared_ptr<const KauaiAudio::Sample> sample;
    CHECK(KauaiAudio::DecodeWave(wave.data(), wave.size(), sample));
    CHECK(!sample->stereo.empty());
    for (float value : sample->stereo)
        CHECK(std::isfinite(value) && value >= -1.01f && value <= 1.01f);
    std::vector<std::uint8_t> imported;
    CHECK(KauaiAudio::PrepareWaveImport(wave.data(), wave.size(), imported));
    CHECK(KauaiAudio::DecodeWave(imported.data(), imported.size(), sample));
#endif
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    Compare(argv[1], "gsm", 1, 8000, 0);
    Compare(argv[1], "mp3", 2, 22050, 2);
    auto gsm = Read(std::string(argv[1]) + "/gsm.wav");
    KauaiAudio::DecodedWave decoded;
    // A partial last GSM block is rounded up on disk and trimmed by fact.
    gsm[48] = 0xdc;
    gsm[49] = 0x05; // 1,500 valid frames out of 1,600 decoded block samples.
    CHECK(KauaiAudio::DecodeCompressedWave(gsm.data(), gsm.size(), decoded) ==
          KauaiAudio::CompressedWaveResult::Decoded);
    CHECK(decoded.samples.size() == 1500);
    // Reject invalid GSM block layout and stereo GSM; leave other codecs to SDL.
    gsm[32] = 64;
    CHECK(KauaiAudio::DecodeCompressedWave(gsm.data(), gsm.size(), decoded) ==
          KauaiAudio::CompressedWaveResult::Invalid);
    gsm[32] = 65;
    gsm[22] = 2;
    CHECK(KauaiAudio::DecodeCompressedWave(gsm.data(), gsm.size(), decoded) ==
          KauaiAudio::CompressedWaveResult::Invalid);
    auto pcm = WaveFixture::Pcm({1, 2, 3});
    CHECK(KauaiAudio::DecodeCompressedWave(pcm.data(), pcm.size(), decoded) ==
          KauaiAudio::CompressedWaveResult::NotHandled);
    auto mp3 = Read(std::string(argv[1]) + "/mp3.wav");
    auto junk = mp3;
    std::fill(junk.begin() + 70, junk.end(), 0); // Keep fmt/fact/data headers, destroy the MP3 frames.
    CHECK(KauaiAudio::DecodeCompressedWave(junk.data(), junk.size(), decoded) ==
          KauaiAudio::CompressedWaveResult::Invalid);
    // Accept a missing terminal RIFF padding byte, as seen in community movies.
    CHECK(mp3.back() == 0);
    mp3.pop_back();
    const auto riffSize = static_cast<std::uint32_t>(mp3.size() - 8);
    for (int byte = 0; byte < 4; ++byte)
        mp3[4 + byte] = static_cast<std::uint8_t>(riffSize >> (byte * 8));
    CHECK(KauaiAudio::DecodeCompressedWave(mp3.data(), mp3.size(), decoded) ==
          KauaiAudio::CompressedWaveResult::Decoded);
    std::puts("GSM/MP3: reference PCM, durations, truncated inputs and legacy RIFF padding passed.");
#ifndef KAUAI_TEST_CODEC_CORE_ONLY
    std::puts("SDL playback conversion and import passed.");
#endif
}
