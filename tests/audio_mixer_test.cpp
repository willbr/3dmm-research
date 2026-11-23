/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "audio_mixer.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#ifdef _WINDOWS_
#error The mixer must not require the Windows SDK
#endif
using namespace KauaiAudio;
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Audio mixer failed: %d\n", __LINE__);                                                \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static Sample Samples(std::initializer_list<float> frames)
{
    auto pcm = static_cast<float *>(std::malloc(frames.size() * 2 * sizeof(float)));
    CHECK(pcm);
    std::size_t at = 0;
    for (auto value : frames)
    {
        pcm[at++] = value;
        pcm[at++] = -value;
    }
    Sample sample(mm_audio_sample_adopt(pcm, frames.size(), std::free));
    CHECK(sample);
    return sample;
}
static MMAudioVoice Sound(int id, const Sample &sample, int plays = 1)
{
    return mm_audio_sample_voice(sample.get(), id, plays);
}
static void Expect(Mixer &mixer, std::initializer_list<float> frames)
{
    std::vector<float> output(frames.size() * 2, 99);
    mixer.Render(output.data(), frames.size());
    std::size_t i = 0;
    for (auto expected : frames)
    {
        CHECK(output[i++] == expected);
        CHECK(output[i++] == -expected);
    }
}
int main()
{
    Mixer mixer;
    const auto a = Samples({0.25f, 0.5f}), b = Samples({0.75f, -0.5f});
    const auto queue = mixer.AddQueue();
    CHECK(queue);
    std::vector<MMAudioVoice> voices = {Sound(1, a, 2), Sound(2, b)};
    CHECK(mixer.Sync(queue, voices));
    Expect(mixer, {0.25f, 0.5f, 0.25f});
    CHECK(!mixer.Done(queue, 1));
    voices[0].paused = true;
    CHECK(mixer.Sync(queue, voices));
    Expect(mixer, {0, 0});
    CHECK(!mixer.Done(queue, 1));
    voices[0].paused = false;
    CHECK(mixer.Sync(queue, voices));
    Expect(mixer, {0.5f, 0.75f, -0.5f});
    CHECK(mixer.Done(queue, 1) && mixer.Done(queue, 2));
    CHECK(mixer.Sync(queue, voices));
    Expect(mixer, {0, 0}); // No resurrection on synchronization.
    auto loop = Sound(3, a, 0);
    CHECK(mixer.Sync(queue, {loop}));
    Expect(mixer, {0.25f, 0.5f, 0.25f});
    mixer.Suspend(true);
    Expect(mixer, {0, 0});
    mixer.Suspend(false);
    Expect(mixer, {0.5f});
    loop.volume = 0.5f;
    CHECK(mixer.Sync(queue, {loop}));
    mixer.SetVolume(0.5f);
    Expect(mixer, {0.0625f, 0.125f});
    mixer.SetVolume(1);
    loop.volume = 1;
    auto invalid = loop;
    invalid.volume = std::numeric_limits<float>::quiet_NaN();
    CHECK(!mixer.Sync(queue, {invalid}) && !mixer.Sync(queue, {loop, loop}));
    invalid = loop;
    invalid.plays = -1;
    CHECK(!mixer.Sync(queue, {invalid}));
    invalid = Sound(loop.id, b, loop.plays);
    CHECK(!mixer.Sync(queue, {invalid}));
    Expect(mixer, {0.125f}); // Rejected updates retained the previous volume/position.
    loop.stopped = true;
    CHECK(mixer.Sync(queue, {loop}));
    CHECK(mixer.Done(queue, 3));
    Expect(mixer, {0});
    auto pcm = static_cast<float *>(std::malloc(192 * sizeof(float)));
    CHECK(pcm);
    for (int i = 0; i < 96; ++i)
    {
        pcm[2 * i] = i < 48 ? 0.25f : 0.75f;
        pcm[2 * i + 1] = -pcm[2 * i];
    }
    const Sample offsetSample(mm_audio_sample_adopt(pcm, 96, std::free));
    CHECK(offsetSample);
    auto offset = Sound(4, offsetSample, 2);
    offset.offset_ms = 3;
    CHECK(mixer.Sync(queue, {offset}));
    std::vector<float> output(48 * 2);
    mixer.Render(output.data(), 48);
    for (int i = 0; i < 48; ++i)
        CHECK(output[2 * i] == 0.75f && output[2 * i + 1] == -0.75f);
    CHECK(mixer.Done(queue, 4));
    mixer.BeginGroup();
    mixer.BeginGroup();
    CHECK(mixer.Sync(queue, {Sound(5, Samples({0.25f}), 0)}));
    const auto second = mixer.AddQueue();
    CHECK(second && mixer.Sync(second, {Sound(6, Samples({0.5f}), 0)}));
    Expect(mixer, {0, 0});
    mixer.EndGroup();
    Expect(mixer, {0});
    mixer.EndGroup();
    Expect(mixer, {0.75f});
    mixer.SetVolume(2);
    Expect(mixer, {1}); // Sum first, then saturate stereo independently.
    mixer.RemoveQueue(second);
    mixer.RemoveQueue(queue);
    Expect(mixer, {0});
    mixer.Lock();
    CHECK(mixer.AddQueue());
    mixer.Unlock();
    CHECK(mixer.Stats().started == 6);
    std::puts("Portable audio mixer: stereo, seamless queues/loops, offsets, pause/suspend, gains/clipping, grouped "
              "starts, transactional updates and cleanup passed.");
}
