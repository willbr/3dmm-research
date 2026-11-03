/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_recording.h"
#include "sdl_audio.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL recording failed: %d\n", __LINE__);                                              \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
int main()
{
    KauaiAudio::Recorder recorder;
    CHECK(!recorder.Start() && !recorder.HaveSound() && !recorder.Play() && recorder.Stop());
    CHECK(!recorder.Open(0, 1, 8, 50));
    for (unsigned bits : {8u, 16u})
        for (unsigned channels : {1u, 2u})
        {
            CHECK(recorder.Open(11025, channels, bits, 50) && recorder.Start() && !recorder.Start());
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (recorder.Recording() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            CHECK(!recorder.Recording() && recorder.HaveSound());
            const auto &wave = recorder.Wave();
            CHECK(wave[20] == 1 && wave[22] == channels && wave[34] == bits);
            CHECK(wave.size() == 44 + ((551 - 8) * channels * (bits / 8) + 1) / 2 * 2);
            std::shared_ptr<const KauaiAudio::Sample> sample;
            CHECK(KauaiAudio::DecodeWave(wave.data(), wave.size(), sample) && !sample->stereo.empty());
            for (float value : sample->stereo)
                CHECK(value == 0);
            CHECK(recorder.Play());
            while (recorder.Playing() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            CHECK(!recorder.Playing() && recorder.HaveSound());
            CHECK(recorder.Play() && recorder.Stop() && !recorder.Playing());
        }
    // Stop explicitly before the cap, then start another take and clear old sound.
    CHECK(recorder.Open(11025, 1, 8, 3000) && recorder.Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(recorder.Stop() && !recorder.Recording() && recorder.HaveSound());
    CHECK(recorder.Start() && !recorder.HaveSound() && recorder.Stop());
    const auto stats = KauaiAudio::RecorderStats();
    CHECK(stats.opened == 5 && stats.started == 6 && stats.completed >= 5 && stats.previewed == 8 &&
          stats.capturedFrames >= 2204);
    std::puts("SDL recording: real dummy-device capture, automatic cap, four PCM layouts, manual stop, repeat takes "
              "and callback preview passed.");
}
