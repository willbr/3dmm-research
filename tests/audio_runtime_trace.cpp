#include "audio_mixer.h"
#include <cstdio>
#include <cstdlib>
using namespace KauaiAudio;
static unsigned hash = 2166136261U;
static void Record(const void *p, std::size_t n)
{
    auto bytes = static_cast<const unsigned char *>(p);
    while (n--)
        hash = (hash ^ *bytes++) * 16777619U;
}
int main()
{
    Mixer mixer;
    auto sample = std::make_shared<Sample>();
    for (int i = 0; i < 997; ++i)
    {
        sample->stereo.push_back(float(i % 31 - 15) / 32);
        sample->stereo.push_back(float(i % 19 - 9) / 16);
    }
    std::uint64_t queues[7];
    std::vector<Voice> voices[7];
    for (auto &queue : queues)
        queue = mixer.AddQueue();
    unsigned random = 12345;
    float output[258];
    for (int step = 0; step < 4000; ++step)
    {
        random = random * 1664525U + 1013904223U;
        unsigned q = (random >> 16) % 7, action = random % 11;
        auto &items = voices[q];
        if (action < 3)
        {
            Voice voice;
            voice.sample = sample;
            voice.id = step + 1;
            voice.plays = (random >> 10) % 4;
            voice.offsetMs = (random >> 20) % 100;
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
        bool ok = mixer.Sync(queues[q], items);
        Record(&ok, sizeof(ok));
        if (action == 7)
            mixer.BeginGroup();
        if (action == 8)
            mixer.EndGroup();
        mixer.Suspend(step % 19 == 0);
        mixer.SetVolume(float(step % 9) / 4);
        auto count = (random >> 22) % 130;
        mixer.Render(output, count);
        Record(output, count * 2 * sizeof(float));
        for (auto &voice : items)
        {
            bool done = mixer.Done(queues[q], voice.id);
            Record(&done, sizeof(done));
        }
        auto stats = mixer.Stats();
        Record(&stats.started, sizeof(stats.started));
        Record(&stats.completed, sizeof(stats.completed));
        Record(&stats.renderedFrames, sizeof(stats.renderedFrames));
    }
    std::printf("%08X\n", hash);
    unsigned short one = 1;
    if (*reinterpret_cast<unsigned char *>(&one) && sizeof(bool) == 1 && sizeof(float) == 4 && hash != 0xD1FA52C3U)
        return 1;
}
