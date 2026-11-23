/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_synth.h"
#include "midi_render.h"
#include "midi_events.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
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
    CHECK(!mm_midi_synth_load("bad", 3, 48000));
    const std::unique_ptr<MMMidiSynth, decltype(&mm_midi_synth_destroy)> synth(
        mm_midi_synth_load(bank.data(), bank.size(), 48000), mm_midi_synth_destroy);
    CHECK(synth);
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
    CHECK(removed && !mm_midi_synth_load(incomplete.data(), incomplete.size(), 48000));
    incomplete.clear();
    incomplete.shrink_to_fit();
    std::vector<float> samples(8192);
    for (int program = 0; program < 128; ++program)
    {
        mm_midi_synth_reset(synth.get());
        mm_midi_synth_render(synth.get(), samples.data(), samples.size() / 2);
        mm_midi_synth_message(synth.get(), 0xC0 | (program << 8));
        mm_midi_synth_message(synth.get(), 0x00643C90);
        mm_midi_synth_render(synth.get(), samples.data(), samples.size() / 2);
        CHECK(Energy(samples) > 0);
    }
    for (int note = 35; note <= 81; ++note)
    {
        mm_midi_synth_reset(synth.get());
        mm_midi_synth_render(synth.get(), samples.data(), samples.size() / 2);
        mm_midi_synth_message(synth.get(), 0x00640099 | (note << 8));
        mm_midi_synth_render(synth.get(), samples.data(), samples.size() / 2);
        CHECK(Energy(samples) > 0);
    }
    mm_midi_synth_reset(synth.get());
    mm_midi_synth_render(synth.get(), samples.data(), samples.size() / 2);
    mm_midi_synth_message(synth.get(), 0x00000BB0);
    mm_midi_synth_message(synth.get(), 0x00643C90); // Expression zero.
    mm_midi_synth_render(synth.get(), samples.data(), samples.size() / 2);
    CHECK(Energy(samples) < 1e-12);
    mm_midi_synth_reset(synth.get());
    mm_midi_synth_render(synth.get(), samples.data(), samples.size() / 2);
    mm_midi_synth_message(synth.get(), 0x00000AB0);
    mm_midi_synth_message(synth.get(), 0x00643C90); // Pan left.
    mm_midi_synth_render(synth.get(), samples.data(), samples.size() / 2);
    double left = 0, right = 0;
    for (std::size_t i = 0; i < samples.size(); i += 2)
    {
        left += samples[i] * samples[i];
        right += samples[i + 1] * samples[i + 1];
    }
    CHECK(left > right * 2);
    const std::unique_ptr<MMMidiDeviceRenderer, decltype(&mm_midi_device_renderer_destroy)> device(
        mm_midi_device_renderer_create(), mm_midi_device_renderer_destroy);
    CHECK(device && mm_midi_device_renderer_load(device.get(), bank.data(), bank.size(), 48000));
    auto renderer = mm_midi_device_renderer_enter(device.get());
    KauaiMidi::StreamEvent delayed[] = {{100, 0, 0x00643C90}, {100, 0, 0x00003C80}};
    CHECK(!mm_midi_device_renderer_queue(device.get(), reinterpret_cast<unsigned char *>(delayed) + 1,
                                        sizeof(delayed[0]), 0, 1, 0));
    CHECK(!mm_midi_device_renderer_queue(device.get(), delayed, sizeof(delayed) - 1, 0, 1, 0));
    CHECK(!mm_midi_device_renderer_queue(device.get(), delayed, sizeof(delayed), 1, 1, 0));
    CHECK(mm_midi_device_renderer_queue(device.get(), delayed, sizeof(delayed), 0, 1, 42));
    CHECK(!mm_midi_device_renderer_load(device.get(), bank.data(), bank.size(), 44100));
    samples.assign(4800 * 2, 9);
    CHECK(!mm_midi_render(renderer, samples.data(), 4800) && Energy(samples) == 0);
    CHECK(renderer->notes.low == 1 && renderer->completed.low == 0);
    CHECK(mm_midi_render(renderer, samples.data(), 4800) && Energy(samples) > 0);
    CHECK(renderer->completed.low == 1 && renderer->frames.low == 9600);
    CHECK(!mm_midi_device_renderer_load(device.get(), bank.data(), bank.size(), 44100));
    void *returnedData;
    std::size_t returnedTag;
    CHECK(mm_midi_render_pop(renderer, &returnedData, &returnedTag) && returnedData == delayed && returnedTag == 42 &&
          !mm_midi_render_pop(renderer, &returnedData, &returnedTag));
    KauaiMidi::StreamEvent repeated[] = {{0, 0, 0x00643C90}, {10, 0, 0x00003C80}};
    CHECK(mm_midi_device_renderer_queue(device.get(), repeated, sizeof(repeated), 0, 3, 99));
    mm_midi_render(renderer, samples.data(), 1439);
    CHECK(renderer->completed.low == 1);
    mm_midi_render(renderer, samples.data(), 1);
    CHECK(renderer->completed.low == 2 && renderer->notes.low == 4);
    CHECK(mm_midi_render_pop(renderer, &returnedData, &returnedTag) && returnedTag == 99);
    // Initial offset excludes the ten-second prefix; subsequent repeats restart at zero.
    KauaiMidi::StreamEvent offset[] = {{10000, 0, KauaiMidi::noOperation << 24}, {10, 0, KauaiMidi::noOperation << 24}};
    CHECK(mm_midi_device_renderer_queue(device.get(), offset, sizeof(offset), sizeof(offset[0]), 2, 7));
    mm_midi_render(renderer, samples.data(), 480);
    CHECK(renderer->completed.low == 2);
    mm_midi_render_stop(renderer);
    CHECK(mm_midi_render_pop(renderer, &returnedData, &returnedTag) && returnedTag == 7 && !renderer->head);
    CHECK(mm_midi_device_renderer_queue(device.get(), repeated, sizeof(repeated), 0, 0, 123)); // Infinite loop.
    mm_midi_render(renderer, samples.data(), 4800);
    CHECK(!mm_midi_render_pop(renderer, &returnedData, &returnedTag));
    mm_midi_render_stop(renderer);
    CHECK(mm_midi_render_pop(renderer, &returnedData, &returnedTag) && returnedTag == 123);
    KauaiMidi::StreamEvent zero[] = {{0, 0, KauaiMidi::noOperation << 24}};
    CHECK(!mm_midi_device_renderer_queue(device.get(), zero, sizeof(zero), 0, 0, 0));
    // A zero-time seek prefix must retain its held note until the subsequent
    // timed buffer arrives; completing the prefix alone is not a note reset.
    mm_midi_render(renderer, samples.data(), 4800);
    KauaiMidi::StreamEvent key[] = {{0, 0, 0x00643C90}};
    KauaiMidi::StreamEvent continuation[] = {{100, 0, 0x00003C80}};
    CHECK(mm_midi_device_renderer_queue(device.get(), key, sizeof(key), 0, 1, 1));
    mm_midi_render(renderer, nullptr, 0);
    CHECK(mm_midi_render_pop(renderer, &returnedData, &returnedTag));
    CHECK(mm_midi_device_renderer_queue(device.get(), continuation, sizeof(continuation), 0, 1, 2));
    mm_midi_render(renderer, samples.data(), 4800);
    CHECK(mm_midi_render_pop(renderer, &returnedData, &returnedTag) && returnedTag == 2);
    CHECK(std::any_of(samples.begin() + 4096, samples.end(), [](float value) { return value != 0; }));
    // Fractional frame carry: 1000 one-tick intervals at 44.1kHz are exactly 44100 frames.
    CHECK(mm_midi_device_renderer_load(device.get(), bank.data(), bank.size(), 44100));
    KauaiMidi::StreamEvent tick[] = {{1, 0, KauaiMidi::noOperation << 24}};
    CHECK(mm_midi_device_renderer_queue(device.get(), tick, sizeof(tick), 0, 1000, 500));
    std::vector<float> second(44100 * 2);
    mm_midi_render(renderer, second.data(), 44099);
    CHECK(!mm_midi_render_pop(renderer, &returnedData, &returnedTag));
    mm_midi_render(renderer, second.data(), 1);
    CHECK(mm_midi_render_pop(renderer, &returnedData, &returnedTag) && returnedTag == 500);
    mm_midi_device_renderer_leave(device.get());
    std::puts("MIDI synth: 128 GM programs, 47 percussion notes, controls, sample timing, repeat/offset/stop and "
              "fractional-frame carry passed.");
}
