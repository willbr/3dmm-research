#include "midi_render.h"
#include "midi_synth.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
// Deterministic codec double: compare scheduling, messages and gain independently
// of SoundFont revisions and platform synthesis differences.
namespace KauaiMidi
{
struct Synth::Impl
{
    unsigned message = 0;
    bool ready = false;
};
Synth::Synth() : impl(new Impl)
{
}
Synth::~Synth() = default;
bool Synth::Load(const void *, std::size_t, int)
{
    impl->ready = true;
    return true;
}
bool Synth::Ready() const
{
    return impl->ready;
}
void Synth::Message(std::uint32_t m)
{
    impl->message = m;
}
void Synth::Reset()
{
    impl->message = 0;
}
void Synth::Silence()
{
    impl->message = 0;
}
void Synth::Render(float *p, std::size_t n)
{
    while (n--)
    {
        *p++ = float(impl->message % 97) / 32;
        *p++ = -float(impl->message % 29) / 16;
    }
}
} // namespace KauaiMidi
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
    Renderer r;
    StreamEvent events[] = {{0, 0, 0x0109abcd},       {7, 0, 0x00643c90}, {13, 0, 0x00003c80}, {0, 0, 0x01000001},
                            {1234567, 0, 0x00433d90}, {0, 0, 0x010fffff}, {19, 0, 0x02000000}};
    r.Load(nullptr, 0, 44100);
    float output[514];
    for (int pass = 0; pass < 31; ++pass)
    {
        auto ok = r.Queue(events, sizeof(events), pass % 7 * sizeof(StreamEvent), pass % 4, pass + 1);
        Record(&ok, sizeof(ok));
        for (int step = 0; step < 79; ++step)
        {
            r.SetVolume(step % 11 == 0 ? std::numeric_limits<float>::quiet_NaN() : float(step % 9) / 8);
            bool ready = r.Render(output, step * 73 % 258);
            Record(&ready, sizeof(ready));
            Record(output, (step * 73 % 258) * 2 * sizeof(float));
            ReturnedBuffer b;
            while (r.PopReturned(b))
                Record(&b.tag, sizeof(b.tag));
        }
        if (pass % 3 == 0)
            r.Stop();
        ReturnedBuffer b;
        while (r.PopReturned(b))
            Record(&b.tag, sizeof(b.tag));
        auto stats = r.Stats();
        Record(&stats.queued, sizeof(stats.queued));
        Record(&stats.completed, sizeof(stats.completed));
        Record(&stats.messages, sizeof(stats.messages));
        Record(&stats.notes, sizeof(stats.notes));
        Record(&stats.frames, sizeof(stats.frames));
        Record(&stats.audibleFrames, sizeof(stats.audibleFrames));
        bool reset = r.Reset();
        Record(&reset, sizeof(reset));
    }
    std::printf("%08X\n", hash);
    unsigned short one = 1;
    if (*reinterpret_cast<unsigned char *>(&one) && sizeof(std::uintptr_t) == 8 && sizeof(bool) == 1 &&
        sizeof(float) == 4 && hash != 0x28316524U)
        return 1;
}
