/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "audio_mixer.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>
#ifdef _WINDOWS_
#error The mixer must not require the Windows SDK
#endif
using Sample = std::unique_ptr<MMAudioSample, decltype(&mm_audio_sample_release)>;
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
    Sample sample(mm_audio_sample_adopt(pcm, frames.size(), std::free), mm_audio_sample_release);
    CHECK(sample);
    return sample;
}
static MMAudioVoice Sound(int id, const Sample &sample, int plays = 1)
{
    return mm_audio_sample_voice(sample.get(), id, plays);
}
static int Sync(MMAudioMixer *mixer, MMAudioCounter queue, const std::vector<MMAudioVoice> &voices)
{
    return mm_audio_mixer_sync(mixer, queue, voices.data(), voices.size());
}
static void Expect(MMAudioMixer *mixer, std::initializer_list<float> frames)
{
    std::vector<float> output(frames.size() * 2, 99);
    mm_audio_mixer_render(mixer, output.data(), frames.size());
    std::size_t i = 0;
    for (auto expected : frames)
    {
        CHECK(output[i++] == expected);
        CHECK(output[i++] == -expected);
    }
}
int main()
{
    const std::unique_ptr<MMAudioDeviceMixer, decltype(&mm_audio_device_mixer_destroy)> device(
        mm_audio_device_mixer_create(), mm_audio_device_mixer_destroy);
    CHECK(device);
    auto mixer = mm_audio_device_mixer_enter(device.get());
    const auto a = Samples({0.25f, 0.5f}), b = Samples({0.75f, -0.5f});
    const auto queue = mm_audio_mixer_add(mixer);
    CHECK(queue.low == 1 && !queue.high);
    std::vector<MMAudioVoice> voices = {Sound(1, a, 2), Sound(2, b)};
    CHECK(Sync(mixer, queue, voices));
    Expect(mixer, {0.25f, 0.5f, 0.25f});
    CHECK(!mm_audio_mixer_done(mixer, queue, 1));
    voices[0].paused = true;
    CHECK(Sync(mixer, queue, voices));
    Expect(mixer, {0, 0});
    CHECK(!mm_audio_mixer_done(mixer, queue, 1));
    voices[0].paused = false;
    CHECK(Sync(mixer, queue, voices));
    Expect(mixer, {0.5f, 0.75f, -0.5f});
    CHECK(mm_audio_mixer_done(mixer, queue, 1) && mm_audio_mixer_done(mixer, queue, 2));
    CHECK(Sync(mixer, queue, voices));
    Expect(mixer, {0, 0}); // No resurrection on synchronization.
    auto loop = Sound(3, a, 0);
    CHECK(Sync(mixer, queue, {loop}));
    Expect(mixer, {0.25f, 0.5f, 0.25f});
    mm_audio_mixer_suspend(mixer, true);
    Expect(mixer, {0, 0});
    mm_audio_mixer_suspend(mixer, false);
    Expect(mixer, {0.5f});
    loop.volume = 0.5f;
    CHECK(Sync(mixer, queue, {loop}));
    mm_audio_mixer_volume(mixer, 0.5f);
    Expect(mixer, {0.0625f, 0.125f});
    mm_audio_mixer_volume(mixer, 1);
    loop.volume = 1;
    auto invalid = loop;
    invalid.volume = std::numeric_limits<float>::quiet_NaN();
    CHECK(!Sync(mixer, queue, {invalid}) && !Sync(mixer, queue, {loop, loop}));
    invalid = loop;
    invalid.plays = -1;
    CHECK(!Sync(mixer, queue, {invalid}));
    invalid = Sound(loop.id, b, loop.plays);
    CHECK(!Sync(mixer, queue, {invalid}));
    Expect(mixer, {0.125f}); // Rejected updates retained the previous volume/position.
    loop.stopped = true;
    CHECK(Sync(mixer, queue, {loop}));
    CHECK(mm_audio_mixer_done(mixer, queue, 3));
    Expect(mixer, {0});
    auto pcm = static_cast<float *>(std::malloc(192 * sizeof(float)));
    CHECK(pcm);
    for (int i = 0; i < 96; ++i)
    {
        pcm[2 * i] = i < 48 ? 0.25f : 0.75f;
        pcm[2 * i + 1] = -pcm[2 * i];
    }
    const Sample offsetSample(mm_audio_sample_adopt(pcm, 96, std::free), mm_audio_sample_release);
    CHECK(offsetSample);
    auto offset = Sound(4, offsetSample, 2);
    offset.offset_ms = 3;
    CHECK(Sync(mixer, queue, {offset}));
    std::vector<float> output(48 * 2);
    mm_audio_mixer_render(mixer, output.data(), 48);
    for (int i = 0; i < 48; ++i)
        CHECK(output[2 * i] == 0.75f && output[2 * i + 1] == -0.75f);
    CHECK(mm_audio_mixer_done(mixer, queue, 4));
    mm_audio_mixer_begin(mixer);
    mm_audio_mixer_begin(mixer);
    CHECK(Sync(mixer, queue, {Sound(5, Samples({0.25f}), 0)}));
    const auto second = mm_audio_mixer_add(mixer);
    CHECK(second.low == 2 && !second.high && Sync(mixer, second, {Sound(6, Samples({0.5f}), 0)}));
    Expect(mixer, {0, 0});
    mm_audio_mixer_end(mixer);
    Expect(mixer, {0});
    mm_audio_mixer_end(mixer);
    Expect(mixer, {0.75f});
    mm_audio_mixer_volume(mixer, 2);
    Expect(mixer, {1}); // Sum first, then saturate stereo independently.
    mm_audio_mixer_remove(mixer, second);
    mm_audio_mixer_remove(mixer, queue);
    Expect(mixer, {0});
    CHECK(mm_audio_device_mixer_enter(device.get()) == mixer);
    const auto projection = mm_audio_mixer_add(mixer);
    mm_audio_device_mixer_leave(device.get());
    CHECK(projection.low == 3 && !projection.high && mixer->started.low == 6 && !mixer->started.high);

    MMSoundQueue soundQueue = {};
    CHECK(mm_sound_queue_init(&soundQueue, nullptr, 0, 0));
    const MMSoundEntry entries[] = {
        {nullptr, 10, 65536, 1, 0, 0, 0, -1},
        {a.get(), 11, 32768, -1, 0, 0, 0, 0},
        {nullptr, 12, 65536, 1, 0, 0, 0, 0}
    };
    CHECK(mm_array_insert(&soundQueue.rows, 0, entries, 2) == MM_STORAGE_OK);
    soundQueue.current = 1;
    const auto sample = [](void *object) -> const MMAudioSample * {
        return static_cast<const MMAudioSample *>(object);
    };
    mm_audio_mixer_volume(mixer, 1);
    CHECK(mm_audio_device_mixer_sync_queue(device.get(), projection, &soundQueue, 65536, sample));
    Expect(mixer, {0.125f});
    CHECK(mm_array_add(&soundQueue.rows, &entries[2], nullptr) == MM_STORAGE_OK);
    CHECK(!mm_audio_device_mixer_sync_queue(device.get(), projection, &soundQueue, 65536, sample));
    Expect(mixer, {0.25f, 0.125f});
    mm_storage_dispose(&soundQueue.storage);
    mm_audio_device_mixer_leave(device.get());
    std::puts("Portable audio mixer: stereo, seamless queues/loops, offsets, pause/suspend, gains/clipping, grouped "
              "starts, transactional updates and cleanup passed.");
}
