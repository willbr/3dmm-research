/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_synth.h"
#include "midi_render.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error MIDI synthesis must not import the Windows SDK
#endif
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "MIDI synthesis failed at %d\n", __LINE__);                                           \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static double Energy(const std::vector<float> &samples)
{
    double energy = 0;
    for (auto sample : samples)
    {
        CHECK(std::isfinite(sample));
        energy += sample * sample;
    }
    return energy;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<char> bank{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!bank.empty());
    KauaiMidi::Synth synth;
    CHECK(!synth.Ready() && !synth.Load("bad", 3, 48000));
    CHECK(synth.Load(bank.data(), bank.size(), 48000) && synth.Ready());
    CHECK(!synth.Load("bad", 3, 48000) && synth.Ready());
    // Keep a structurally valid bank while removing one GM program. Loading
    // it must fail rather than silently substituting piano for that instrument.
    auto incomplete = bank;
    bool removed = false;
    for (std::size_t at = 0; at + 8 < incomplete.size(); ++at)
        if (incomplete[at] == 'p' && incomplete[at + 1] == 'h' && incomplete[at + 2] == 'd' &&
            incomplete[at + 3] == 'r')
        {
            const auto byte = [&](std::size_t index) { return static_cast<unsigned char>(incomplete[index]); };
            const auto bytes = unsigned(byte(at + 4)) | (unsigned(byte(at + 5)) << 8) | (unsigned(byte(at + 6)) << 16) |
                               (unsigned(byte(at + 7)) << 24);
            if (bytes % 38 || bytes > incomplete.size() - at - 8)
                continue;
            for (std::size_t entry = at + 8; entry + 38 <= at + 8 + bytes; entry += 38)
                if (byte(entry + 20) == 91 && !byte(entry + 21) && !byte(entry + 22) && !byte(entry + 23))
                {
                    incomplete[entry + 22] = 127;
                    removed = true;
                }
            break;
        }
    CHECK(removed && !synth.Load(incomplete.data(), incomplete.size(), 48000) && synth.Ready());
    incomplete.clear();
    incomplete.shrink_to_fit();
    std::vector<float> samples(8192);
    for (int program = 0; program < 128; ++program)
    {
        synth.Reset();
        synth.Render(samples.data(), samples.size() / 2);
        synth.Message(0xC0 | (program << 8));
        synth.Message(0x00643C90);
        synth.Render(samples.data(), samples.size() / 2);
        CHECK(Energy(samples) > 0);
    }
    for (int note = 35; note <= 81; ++note)
    {
        synth.Reset();
        synth.Render(samples.data(), samples.size() / 2);
        synth.Message(0x00640099 | (note << 8));
        synth.Render(samples.data(), samples.size() / 2);
        CHECK(Energy(samples) > 0);
    }
    synth.Reset();
    synth.Render(samples.data(), samples.size() / 2);
    synth.Message(0x00000BB0);
    synth.Message(0x00643C90); // Expression zero.
    synth.Render(samples.data(), samples.size() / 2);
    CHECK(Energy(samples) < 1e-12);
    synth.Reset();
    synth.Render(samples.data(), samples.size() / 2);
    synth.Message(0x00000AB0);
    synth.Message(0x00643C90); // Pan left.
    synth.Render(samples.data(), samples.size() / 2);
    double left = 0, right = 0;
    for (std::size_t i = 0; i < samples.size(); i += 2)
    {
        left += samples[i] * samples[i];
        right += samples[i + 1] * samples[i + 1];
    }
    CHECK(left > right * 2);
    KauaiMidi::Renderer renderer;
    CHECK(renderer.Load(bank.data(), bank.size(), 48000));
    KauaiMidi::StreamEvent delayed[] = {{100, 0, 0x00643C90}, {100, 0, 0x00003C80}};
    CHECK(!renderer.Queue(delayed, sizeof(delayed) - 1, 0, 1, 0));
    CHECK(!renderer.Queue(delayed, sizeof(delayed), 1, 1, 0));
    CHECK(renderer.Queue(delayed, sizeof(delayed), 0, 1, 42));
    samples.assign(4800 * 2, 9);
    CHECK(!renderer.Render(samples.data(), 4800) && Energy(samples) == 0);
    CHECK(renderer.Stats().notes == 1 && renderer.Stats().completed == 0);
    CHECK(renderer.Render(samples.data(), 4800) && Energy(samples) > 0);
    CHECK(renderer.Stats().completed == 1 && renderer.Stats().frames == 9600);
    KauaiMidi::ReturnedBuffer returned;
    CHECK(renderer.PopReturned(returned) && returned.data == delayed && returned.tag == 42 &&
          !renderer.PopReturned(returned));
    KauaiMidi::StreamEvent repeated[] = {{0, 0, 0x00643C90}, {10, 0, 0x00003C80}};
    CHECK(renderer.Queue(repeated, sizeof(repeated), 0, 3, 99));
    renderer.Render(samples.data(), 1439);
    CHECK(renderer.Stats().completed == 1);
    renderer.Render(samples.data(), 1);
    CHECK(renderer.Stats().completed == 2 && renderer.Stats().notes == 4);
    CHECK(renderer.PopReturned(returned) && returned.tag == 99);
    // Initial offset excludes the ten-second prefix; subsequent repeats restart at zero.
    KauaiMidi::StreamEvent offset[] = {{10000, 0, KauaiMidi::noOperation << 24}, {10, 0, KauaiMidi::noOperation << 24}};
    CHECK(renderer.Queue(offset, sizeof(offset), sizeof(offset[0]), 2, 7));
    renderer.Render(samples.data(), 480);
    CHECK(renderer.Stats().completed == 2);
    renderer.Stop();
    CHECK(renderer.PopReturned(returned) && returned.tag == 7 && !renderer.Pending());
    CHECK(renderer.Queue(repeated, sizeof(repeated), 0, 0, 123)); // Infinite loop.
    renderer.Render(samples.data(), 4800);
    CHECK(!renderer.PopReturned(returned));
    renderer.Stop();
    CHECK(renderer.PopReturned(returned) && returned.tag == 123);
    KauaiMidi::StreamEvent zero[] = {{0, 0, KauaiMidi::noOperation << 24}};
    CHECK(!renderer.Queue(zero, sizeof(zero), 0, 0, 0));
    // A zero-time seek prefix must retain its held note until the subsequent
    // timed buffer arrives; completing the prefix alone is not a note reset.
    renderer.Render(samples.data(), 4800);
    KauaiMidi::StreamEvent key[] = {{0, 0, 0x00643C90}};
    KauaiMidi::StreamEvent continuation[] = {{100, 0, 0x00003C80}};
    CHECK(renderer.Queue(key, sizeof(key), 0, 1, 1));
    renderer.Render(nullptr, 0);
    CHECK(renderer.PopReturned(returned));
    CHECK(renderer.Queue(continuation, sizeof(continuation), 0, 1, 2));
    renderer.Render(samples.data(), 4800);
    CHECK(renderer.PopReturned(returned) && returned.tag == 2);
    CHECK(std::any_of(samples.begin() + 4096, samples.end(), [](float value) { return value != 0; }));
    // Fractional frame carry: 1000 one-tick intervals at 44.1kHz are exactly 44100 frames.
    CHECK(renderer.Load(bank.data(), bank.size(), 44100));
    KauaiMidi::StreamEvent tick[] = {{1, 0, KauaiMidi::noOperation << 24}};
    CHECK(renderer.Queue(tick, sizeof(tick), 0, 1000, 500));
    std::vector<float> second(44100 * 2);
    renderer.Render(second.data(), 44099);
    CHECK(!renderer.PopReturned(returned));
    renderer.Render(second.data(), 1);
    CHECK(renderer.PopReturned(returned) && returned.tag == 500);
    std::puts("MIDI synth: 128 GM programs, 47 percussion notes, controls, sample timing, repeat/offset/stop and "
              "fractional-frame carry passed.");
}
