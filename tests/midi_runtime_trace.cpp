#include "midi_render.h"
#include "midi_synth.h"
#include "midi_events.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
// Deterministic codec double: compare scheduling, messages and gain independently
// of SoundFont revisions and platform synthesis differences.
struct tsf
{
    MMU32 message;
};

MMMidiSynth *mm_midi_synth_load(const void *, std::size_t, int)
{
    return static_cast<MMMidiSynth *>(std::calloc(1, sizeof(MMMidiSynth)));
}
void mm_midi_synth_destroy(void *p)
{
    std::free(p);
}
void mm_midi_synth_message(void *p, MMU32 message)
{
    static_cast<MMMidiSynth *>(p)->message = message;
}
void mm_midi_synth_reset(void *p)
{
    static_cast<MMMidiSynth *>(p)->message = 0;
}
void mm_midi_synth_silence(void *p)
{
    mm_midi_synth_reset(p);
}
void mm_midi_synth_render(void *p, float *out, std::size_t frames)
{
    const auto message = static_cast<const MMMidiSynth *>(p)->message;
    while (frames--)
    {
        *out++ = float(message % 97) / 32;
        *out++ = -float(message % 29) / 16;
    }
}
static unsigned hash = 2166136261U;
static void Record(const void *p, std::size_t n)
{
    auto b = static_cast<const unsigned char *>(p);
    while (n--)
        hash = (hash ^ *b++) * 16777619U;
}
int main()
{
    using namespace KauaiMidi;
    const std::unique_ptr<MMMidiDeviceRenderer, decltype(&mm_midi_device_renderer_destroy)> device(
        mm_midi_device_renderer_create(), mm_midi_device_renderer_destroy);
    if (!device || !mm_midi_device_renderer_load(device.get(), nullptr, 0, 44100))
        return 1;
    auto r = mm_midi_device_renderer_enter(device.get());
    StreamEvent events[] = {{0, 0, 0x0109abcd},       {7, 0, 0x00643c90}, {13, 0, 0x00003c80}, {0, 0, 0x01000001},
                            {1234567, 0, 0x00433d90}, {0, 0, 0x010fffff}, {19, 0, 0x02000000}};
    float output[514];
    for (int pass = 0; pass < 31; ++pass)
    {
        bool ok = mm_midi_device_renderer_queue(device.get(), events, sizeof(events),
                                               pass % 7 * sizeof(StreamEvent), pass % 4, pass + 1);
        Record(&ok, sizeof(ok));
        for (int step = 0; step < 79; ++step)
        {
            mm_midi_render_volume(r, step % 11 == 0 ? std::numeric_limits<float>::quiet_NaN() : float(step % 9) / 8);
            bool ready = mm_midi_render(r, output, step * 73 % 258);
            Record(&ready, sizeof(ready));
            Record(output, (step * 73 % 258) * 2 * sizeof(float));
            void *buffer;
            std::size_t tag;
            while (mm_midi_render_pop(r, &buffer, &tag))
                Record(&tag, sizeof(tag));
        }
        if (pass % 3 == 0)
            mm_midi_render_stop(r);
        void *buffer;
        std::size_t tag;
        while (mm_midi_render_pop(r, &buffer, &tag))
            Record(&tag, sizeof(tag));
        const MMAudioCounter counters[] = {r->queued, r->completed, r->messages,
                                           r->notes, r->frames, r->audible_frames};
        for (const auto counter : counters)
        {
            const std::uint64_t value = (std::uint64_t(counter.high) << 32) | counter.low;
            Record(&value, sizeof(value));
        }
        bool reset = mm_midi_render_reset(r);
        Record(&reset, sizeof(reset));
    }
    mm_midi_device_renderer_leave(device.get());
    std::printf("%08X\n", hash);
    unsigned short one = 1;
    if (*reinterpret_cast<unsigned char *>(&one) && sizeof(std::uintptr_t) == 8 && sizeof(bool) == 1 &&
        sizeof(float) == 4 && hash != 0x28316524U)
        return 1;
}
