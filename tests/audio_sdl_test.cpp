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
    auto bytes = WaveFixture::Pcm({-32768, 0, 16384, 32767});
    std::shared_ptr<const KauaiAudio::Sample> sample;
    CHECK(KauaiAudio::DecodeWave(bytes.data(), bytes.size(), sample) && sample->stereo.size() == 8);
    const float expected[] = {-1, 0, 0.5f, 32767.0f / 32768};
    for (int i = 0; i < 4; ++i)
        CHECK(sample->stereo[2 * i] == expected[i] && sample->stereo[2 * i + 1] == expected[i]);
    bytes = WaveFixture::Pcm8(std::vector<std::uint8_t>(22050, 128), 22050);
    CHECK(KauaiAudio::DecodeWave(bytes.data(), bytes.size(), sample) && sample->stereo.size() == 96000);
    for (auto value : sample->stereo)
        CHECK(value == 0);
    bytes = WaveFixture::Adpcm();
    CHECK(KauaiAudio::DecodeWave(bytes.data(), bytes.size(), sample) && sample->stereo.size() == 8);
    for (int i = 0; i < 4; ++i)
        CHECK(sample->stereo[2 * i] == (i ? 1.0f : -1.0f) / 32 && sample->stereo[2 * i + 1] == sample->stereo[2 * i]);
    auto retained = sample;
    CHECK(!KauaiAudio::DecodeWave("bad", 3, sample) && sample == retained);
    bytes[0] = 'X';
    CHECK(!KauaiAudio::DecodeWave(bytes.data(), bytes.size(), sample) && sample == retained);
    KauaiAudio::Output output;
    CHECK(output.Open());
    auto queue = output.Engine().AddQueue();
    CHECK(queue);
    KauaiAudio::Voice voice;
    voice.id = 1;
    voice.sample = sample;
    CHECK(output.Engine().Sync(queue, {voice}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!output.Engine().Done(queue, 1) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(output.Engine().Done(queue, 1) && output.Engine().Stats().started == 1 &&
          output.Engine().Stats().completed == 1);
    std::puts("SDL audio: exact PCM/MS ADPCM decode, rejected input, and real dummy-device callback playback passed.");
}
