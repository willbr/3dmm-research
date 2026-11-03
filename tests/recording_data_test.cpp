/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "recording_data.h"
#include <cstdio>
#include <cstdlib>
#ifdef _WINDOWS_
#error Recording data must not import the Windows SDK
#endif
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Recording data failed: %d\n", __LINE__);                                             \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static unsigned Dword(const std::vector<std::uint8_t> &v, unsigned at)
{
    return v[at] | (unsigned(v[at + 1]) << 8) | (unsigned(v[at + 2]) << 16) | (unsigned(v[at + 3]) << 24);
}
int main()
{
    KauaiAudio::RecordingBuffer capture;
    std::vector<std::uint8_t> wave{1, 2, 3};
    CHECK(!capture.Full() && !capture.Frames() && !capture.Wave(wave) && wave.size() == 3);
    CHECK(!capture.Init(0, 1, 8, 1000) && !capture.Init(1000, 0, 8, 1000) && !capture.Init(1000, 1, 24, 1000) &&
          !capture.Init(1000, 1, 8, UINT32_MAX));
    CHECK(capture.Init(1000, 1, 8, 2008));
    std::vector<std::uint8_t> input(2008, 160);
    for (unsigned i = 600; i < 800; ++i)
        input[i] = (i & 1) ? 180 : 140;
    for (unsigned i = 2000; i < input.size(); ++i)
        input[i] = 255;
    CHECK(capture.Append(input.data(), 317) == 317 && capture.Frames() == 317 && !capture.Full());
    CHECK(capture.Append(input.data() + 317, input.size() - 317) == input.size() - 317 && capture.Full());
    CHECK(capture.Append(input.data(), input.size()) == 0 && capture.Frames() == 2008);
    CHECK(!capture.Init(1000, 3, 8, 1) && capture.Full());
    CHECK(capture.Wave(wave) && Dword(wave, 40) == 949 && Dword(wave, 24) == 1000);
    for (unsigned i = 0; i < 949; ++i)
        CHECK(wave[44 + i] == (i >= 249 && i < 449 ? (((i + 351) & 1) ? 148 : 108) : 128));
    const auto encoded = wave;
    CHECK(capture.Wave(wave) && wave == encoded);
    capture.Reset();
    CHECK(!capture.Frames() && !capture.Full() && !capture.Wave(wave) && wave == encoded);
    CHECK(capture.Append(input.data(), 8) == 8 && !capture.Wave(wave) && wave == encoded);
    capture.Reset();
    input.assign(1008, 160);
    CHECK(capture.Append(input.data(), input.size()) == input.size() && capture.Wave(wave));
    CHECK(Dword(wave, 40) == 1000);
    for (unsigned i = 44; i < wave.size(); ++i)
        CHECK(wave[i] == 128);
    // Stereo LE16 keeps both channels and their interleaving through bias/trim.
    CHECK(capture.Init(1000, 2, 16, 2008));
    input.clear();
    for (unsigned frame = 0; frame < 2008; ++frame)
        for (unsigned channel = 0; channel < 2; ++channel)
        {
            const auto sample =
                static_cast<std::uint16_t>(frame >= 600 && frame < 800 ? (channel ? -2000 : 6000) : 2000);
            input.push_back(sample & 255);
            input.push_back(sample >> 8);
        }
    CHECK(!capture.Append(input.data(), 3) && !capture.Frames());
    CHECK(capture.Append(input.data(), input.size()) == input.size() && capture.Wave(wave));
    CHECK(wave[22] == 2 && wave[32] == 4 && wave[34] == 16 && Dword(wave, 28) == 4000 && Dword(wave, 40) == 3796);
    for (unsigned frame = 0; frame < 949; ++frame)
        for (unsigned channel = 0; channel < 2; ++channel)
        {
            const unsigned at = 44 + frame * 4 + channel * 2;
            const unsigned sample = wave[at] | (unsigned(wave[at + 1]) << 8);
            CHECK(sample == (frame >= 249 && frame < 449 ? static_cast<std::uint16_t>(channel ? -4000 : 4000) : 0));
        }
    std::puts("Portable recording: bounded capture, reset, short-tail safety, DC bias, silence margins, silence "
              "retention, stereo LE16 and transactional failures passed.");
}
