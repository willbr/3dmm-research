/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "video_sdl.h"
#include "avi_fixture.h"
#include <chrono>
#include <thread>
#include <cstdio>
#define CHECK(v)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(v))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL video failed at %d: %s\n", __LINE__, #v);                                        \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static void Wait(unsigned ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
int main()
{
    using namespace KauaiVideo;
    SDLPlayback player;
    CHECK(player.Info().frames == 0 && !player.Play(0));
    for (bool sixteen : {false, true})
    {
        auto bytes = AviFixture::Fixture(true, sixteen);
        CHECK(player.Open(bytes.data(), bytes.size(), true) && player.Info().frames == 3);
        KauaiPixels::DibFrame image;
        CHECK(player.DecodeFrame(0, image) && image.width == 4 && image.pixels[0] == 7);
        CHECK(player.DecodeFrame(2, image) && image.pixels[0] == 33);
        const auto before = SDLVideoStats();
        CHECK(player.Play(0) && player.Playing());
        Wait(110);
        player.Stop();
        const auto stopped = player.Position();
        CHECK(!player.Playing() && stopped < 2);
        Wait(80);
        CHECK(player.Position() == stopped);
        CHECK(player.Play(0));
        // An emulated audio device can progress slower than wall time. The
        // player must follow that device, including the complete final frame.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (player.Playing() && player.Position() < 2 && std::chrono::steady_clock::now() < deadline)
            Wait(2);
        CHECK(player.Playing() && player.Position() == 2);
        while (player.Playing() && std::chrono::steady_clock::now() < deadline)
            Wait(2);
        CHECK(!player.Playing() && player.Position() == 2);
        auto after = SDLVideoStats();
        CHECK(after.started == before.started + 2 && after.completed == before.completed + 1);
        CHECK(after.consumedAudioBytes > before.consumedAudioBytes &&
              after.audibleAudioFrames > before.audibleAudioFrames);
        CHECK(player.Play(2)); // Seek/resume at the final frame, including fractional frame times.
        Wait(50);
        CHECK(player.Playing() && player.Position() == 2);
        Wait(250);
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (player.Playing() && std::chrono::steady_clock::now() < deadline)
            Wait(2);
        CHECK(!player.Playing() && player.Position() == 2);
        CHECK(!player.Play(-1) && !player.Play(3));
        CHECK(!player.SetGain(-1) && !player.SetGain(3));
        CHECK(player.SetGain(0));
        const auto muted = SDLVideoStats().audibleAudioFrames;
        CHECK(player.Play(2));
        Wait(250);
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (player.Playing() && std::chrono::steady_clock::now() < deadline)
            Wait(2);
        CHECK(!player.Playing() && SDLVideoStats().audibleAudioFrames == muted);
        CHECK(player.SetGain(1));
        CHECK(player.Open(bytes.data(), bytes.size(), false));
        const auto silent = SDLVideoStats().queuedAudioBytes;
        CHECK(player.Play(2));
        Wait(200);
        CHECK(!player.Playing() && player.Position() == 2 && SDLVideoStats().queuedAudioBytes == silent);
    }
    CHECK(!player.Open("bad", 3, true) && player.Info().frames == 0 && !player.Play(0));
    player.Close();
    std::puts("SDL video: real U8/S16 device output, audible postmix, shared audio clock, stop/restart, final-frame "
              "duration, seek, silent mode and failed reopen passed.");
}
