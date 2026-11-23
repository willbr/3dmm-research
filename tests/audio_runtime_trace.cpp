#include "audio_mixer.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <vector>
static unsigned hash = 2166136261U;
static void Record(const void *p, std::size_t n)
{
    auto bytes = static_cast<const unsigned char *>(p);
    while (n--)
        hash = (hash ^ *bytes++) * 16777619U;
}
int main()
{
    const std::unique_ptr<MMAudioDeviceMixer, decltype(&mm_audio_device_mixer_destroy)> device(
        mm_audio_device_mixer_create(), mm_audio_device_mixer_destroy);
    if (!device)
        return 1;
    auto pcm = static_cast<float *>(std::malloc(997 * 2 * sizeof(float)));
    if (!pcm)
        return 1;
    for (int i = 0; i < 997; ++i)
    {
        pcm[2 * i] = float(i % 31 - 15) / 32;
        pcm[2 * i + 1] = float(i % 19 - 9) / 16;
    }
    const std::unique_ptr<MMAudioSample, decltype(&mm_audio_sample_release)> sample(
        mm_audio_sample_adopt(pcm, 997, std::free), mm_audio_sample_release);
    if (!sample)
    {
        std::free(pcm);
        return 1;
    }
    auto mixer = mm_audio_device_mixer_enter(device.get());
    MMAudioCounter queues[7];
    std::vector<MMAudioVoice> voices[7];
    for (auto &queue : queues)
        queue = mm_audio_mixer_add(mixer);
    unsigned random = 12345;
    float output[258];
    for (int step = 0; step < 4000; ++step)
    {
        random = random * 1664525U + 1013904223U;
        unsigned q = (random >> 16) % 7, action = random % 11;
        auto &items = voices[q];
        if (action < 3)
        {
            auto voice = mm_audio_sample_voice(sample.get(), step + 1, (random >> 10) % 4);
            voice.offset_ms = (random >> 20) % 100;
            voice.volume = float((random >> 15) % 7) / 4;
            items.push_back(voice);
        }
        else if (!items.empty())
        {
            if (action == 3)
                items.erase(items.begin());
            if (action == 4)
                items.front().paused = !items.front().paused;
            if (action == 5)
                items.front().stopped = true;
            if (action == 6)
                items.back().volume = float(step % 8) / 4;
        }
        bool ok = mm_audio_mixer_sync(mixer, queues[q], items.data(), items.size());
        Record(&ok, sizeof(ok));
        if (action == 7)
            mm_audio_mixer_begin(mixer);
        if (action == 8)
            mm_audio_mixer_end(mixer);
        mm_audio_mixer_suspend(mixer, step % 19 == 0);
        mm_audio_mixer_volume(mixer, float(step % 9) / 4);
        auto count = (random >> 22) % 130;
        mm_audio_mixer_render(mixer, output, count);
        Record(output, count * 2 * sizeof(float));
        for (auto &voice : items)
        {
            bool done = mm_audio_mixer_done(mixer, queues[q], voice.id);
            Record(&done, sizeof(done));
        }
        const MMAudioCounter counters[] = {mixer->started, mixer->completed, mixer->rendered_frames};
        for (const auto counter : counters)
        {
            const std::uint64_t value = (std::uint64_t(counter.high) << 32) | counter.low;
            Record(&value, sizeof(value));
        }
    }
    mm_audio_device_mixer_leave(device.get());
    std::printf("%08X\n", hash);
    unsigned short one = 1;
    if (*reinterpret_cast<unsigned char *>(&one) && sizeof(bool) == 1 && sizeof(float) == 4 && hash != 0xD1FA52C3U)
        return 1;
}
