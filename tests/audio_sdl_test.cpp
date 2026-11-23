/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_audio.h"
#include "wave_fixture.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL audio failed: %d\n", __LINE__);                                                  \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
int main()
{
    // Length-only accounting must match the actual SDL conversion, including
    // fractional rate ratios, short clips, fact trimming and malformed data.
    for (unsigned rate : {8000u, 11025u, 22050u, 44100u, 48000u})
        for (unsigned frames : {1u, 7u, 1001u})
        {
            const auto wave = WaveFixture::Pcm8(std::vector<std::uint8_t>(frames, 128), rate);
            KauaiAudio::Sample decoded;
            double seconds = -1;
            CHECK(KauaiAudio::DecodeWave(wave.data(), wave.size(), decoded));
            CHECK(KauaiAudio::WaveSeconds(wave.data(), wave.size(), seconds));
            CHECK(seconds == double(mm_audio_sample_frames(decoded.get())) / KauaiAudio::SampleRate);
        }
    {
        const auto wave = WaveFixture::Adpcm();
        KauaiAudio::Sample decoded;
        double seconds = -1;
        CHECK(KauaiAudio::DecodeWave(wave.data(), wave.size(), decoded));
        CHECK(KauaiAudio::WaveSeconds(wave.data(), wave.size(), seconds));
        CHECK(seconds == double(mm_audio_sample_frames(decoded.get())) / KauaiAudio::SampleRate);
        CHECK(!KauaiAudio::WaveSeconds("bad", 3, seconds) && seconds == 0);
    }
    auto bytes = WaveFixture::Pcm({-32768, 0, 16384, 32767});
    KauaiAudio::Sample sample;
    CHECK(KauaiAudio::DecodeWave(bytes.data(), bytes.size(), sample) && mm_audio_sample_frames(sample.get()) == 4);
    const float expected[] = {-1, 0, 0.5f, 32767.0f / 32768};
    for (int i = 0; i < 4; ++i)
        CHECK(mm_audio_sample_data(sample.get())[2 * i] == expected[i] &&
              mm_audio_sample_data(sample.get())[2 * i + 1] == expected[i]);
    bytes = WaveFixture::Pcm8(std::vector<std::uint8_t>(22050, 128), 22050);
    CHECK(KauaiAudio::DecodeWave(bytes.data(), bytes.size(), sample) && mm_audio_sample_frames(sample.get()) == 48000);
    for (std::size_t i = 0; i < mm_audio_sample_frames(sample.get()) * 2; ++i)
        CHECK(mm_audio_sample_data(sample.get())[i] == 0);
    bytes = WaveFixture::Adpcm();
    CHECK(KauaiAudio::DecodeWave(bytes.data(), bytes.size(), sample) && mm_audio_sample_frames(sample.get()) == 4);
    const auto pcm = mm_audio_sample_data(sample.get());
    for (int i = 0; i < 4; ++i)
        CHECK(pcm[2 * i] == (i ? 1.0f : -1.0f) / 32 && pcm[2 * i + 1] == pcm[2 * i]);
    const auto retained = sample.get();
    CHECK(!KauaiAudio::DecodeWave("bad", 3, sample) && sample.get() == retained);
    bytes[0] = 'X';
    CHECK(!KauaiAudio::DecodeWave(bytes.data(), bytes.size(), sample) && sample.get() == retained);
    const std::unique_ptr<MMSdlAudioOutput, decltype(&mm_sdl_audio_output_destroy)> output(
        mm_sdl_audio_output_create_mixer(), mm_sdl_audio_output_destroy);
    CHECK(output && mm_sdl_audio_output_open(output.get(), nullptr, nullptr));
    const auto queue = mm_audio_mixer_add(KauaiAudio::LockedMixer(output.get()).Get());
    CHECK(queue.low && !queue.high);
    const auto voice = mm_audio_sample_voice(sample.get(), 1, 1);
    CHECK(mm_audio_mixer_sync(KauaiAudio::LockedMixer(output.get()).Get(), queue, &voice, 1));
    sample.reset();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!mm_audio_mixer_done(KauaiAudio::LockedMixer(output.get()).Get(), queue, 1) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const KauaiAudio::LockedMixer lock(output.get());
    const auto mixer = lock.Get();
    CHECK(mm_audio_mixer_done(mixer, queue, 1) && mixer->started.low == 1 && !mixer->started.high &&
          mixer->completed.low == 1 && !mixer->completed.high);
    std::puts("SDL audio: exact PCM/MS ADPCM decode, rejected input, and real dummy-device callback playback passed.");
}
