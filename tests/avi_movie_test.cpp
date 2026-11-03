/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "avi_movie.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(v)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(v))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "AVI failed at %d: %s\n", __LINE__, #v);                                              \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
#include "avi_fixture.h"
using namespace AviFixture;
int main()
{
    using namespace KauaiVideo;
    Bytes pixels(8 * 4, 55);
    const Bytes stream{0x01, 0x20, 10, 20, 0x01, 0x90, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0};
    CHECK(DecodeVideo1(stream.data(), stream.size(), 8, 4, pixels));
    // Independent top-down expected matrix for one two-color and one eight-color tile.
    const Bytes expected{20, 10, 20, 20, 5, 6, 8, 7, 20, 20, 20, 20, 6, 6, 8, 8,
                         20, 20, 20, 20, 2, 2, 4, 4, 10, 20, 20, 20, 1, 2, 4, 4};
    CHECK(pixels == expected);
    const Bytes skip{2, 0x84};
    CHECK(DecodeVideo1(skip.data(), skip.size(), 8, 4, pixels) && pixels == expected);
    for (const Bytes bad :
         {Bytes{0, 0x84}, Bytes{3, 0x84}, Bytes{0, 0}, Bytes{1, 0x90, 2}, Bytes{1, 0x80}, Bytes{1, 0x84}})
        CHECK(!DecodeVideo1(bad.data(), bad.size(), 8, 4, pixels) && pixels == expected);
    CHECK(!DecodeVideo1(nullptr, 8, 8, 4, pixels));
    CHECK(!DecodeVideo1(stream.data(), stream.size(), 7, 4, pixels));
    Bytes tall(4 * 1040, 55);
    const Bytes longSkip{3, 0x85, 25, 0x80};
    CHECK(DecodeVideo1(longSkip.data(), longSkip.size(), 4, 1040, tall));
    CHECK(std::all_of(tall.begin(), tall.begin() + 16, [](std::uint8_t p) { return p == 25; }));
    CHECK(std::all_of(tall.begin() + 16, tall.end(), [](std::uint8_t p) { return p == 55; }));
    auto fixture = Fixture();
    const auto original = fixture;
    AviMovie movie;
    CHECK(movie.Info().frames == 0 && movie.TimeForFrame(0) == -1);
    CHECK(movie.Read(fixture.data(), fixture.size()));
    // The parser owns its bytes; caller storage can disappear.
    std::fill(fixture.begin(), fixture.end(), 0);
    CHECK(movie.Info().frames == 3 && movie.Info().width == 4 && movie.Info().start == 3);
    CHECK(movie.TimeForFrame(0) == 500 && movie.TimeForFrame(3) == 1000);
    CHECK(movie.FrameForTime(500) == -1 && movie.FrameForTime(501) == 0);
    CHECK(movie.FrameForTime(1000) == 2 && movie.FrameForTime(-1000) == -4);
    CHECK(movie.FrameForTime(INT32_MAX) == 3 && movie.FrameForTime(INT32_MIN) == -4);
    CHECK(movie.Audio().pcm == Bytes({128, 129, 130, 131}) && movie.Audio().rate == 22050);
    KauaiPixels::DibFrame frame;
    for (int index : {0, 1, 2, 0, 2, 1})
    {
        CHECK(movie.DecodeFrame(index, frame) && frame.sourceDepth == 32 && frame.pitch == 16);
        for (int i = 0; i < 16; ++i)
        {
            CHECK(frame.pixels[i * 4] == (index ? 33 : 7));
            CHECK(frame.pixels[i * 4 + 1] == (index ? 22 : 248));
            CHECK(frame.pixels[i * 4 + 2] == (index ? 11 : 7) && frame.pixels[i * 4 + 3] == 0);
        }
    }
    const auto prior = frame.pixels;
    CHECK(!movie.DecodeFrame(-1, frame) && !movie.DecodeFrame(3, frame) && frame.pixels == prior);
    CHECK(!movie.Read(nullptr, original.size()) && movie.Info().frames == 3);
    // Unsupported codec, impossible geometry, timeline overflow, missing palette.
    const auto strf = Payload(original, "strf"), strh = Payload(original, "strh");
    for (const auto &field : {std::pair<std::size_t, std::uint32_t>{strf + 4, 0x80000000u},
                              {strf + 8, UINT32_MAX},
                              {strf + 16, 0},
                              {strf + 32, 257},
                              {strh + 20, 0},
                              {strh + 24, 0},
                              {strh + 28, UINT32_MAX},
                              {strh + 32, UINT32_MAX}})
    {
        auto bad = original;
        Put32(bad, field.first, field.second);
        CHECK(!movie.Read(bad.data(), bad.size()) && movie.Info().frames == 3);
    }
    auto overflowTime = original;
    Put32(overflowTime, strh + 20, UINT32_MAX);
    Put32(overflowTime, strh + 24, 1);
    CHECK(!movie.Read(overflowTime.data(), overflowTime.size()) && movie.Info().frames == 3);
    auto badPacket = original;
    const auto packet = Payload(badPacket, "00dc");
    badPacket[packet] = 0;
    badPacket[packet + 1] = 0x84; // A zero-length skip cannot advance a frame.
    AviMovie broken;
    CHECK(broken.Read(badPacket.data(), badPacket.size()) && !broken.DecodeFrame(0, frame) && frame.pixels == prior);
    // Restore the outer RIFF length on each truncation so nested bounds are exercised too.
    for (std::size_t n = 0; n < original.size(); ++n)
    {
        auto bad = original;
        if (n >= 8)
            Put32(bad, 4, static_cast<std::uint32_t>(n - 8));
        CHECK(!movie.Read(bad.data(), n) && movie.Info().frames == 3);
    }
    // Corrupt every RIFF chunk length, including lists and odd-sized JUNK.
    for (std::size_t i = 0; i + 8 < original.size(); ++i)
        if (!std::memcmp(original.data() + i, "LIST", 4) || !std::memcmp(original.data() + i, "strh", 4) ||
            !std::memcmp(original.data() + i, "00dc", 4) || !std::memcmp(original.data() + i, "JUNK", 4))
        {
            auto bad = original;
            Put32(bad, i + 4, UINT32_MAX);
            CHECK(!movie.Read(bad.data(), bad.size()) && movie.DecodeFrame(0, frame));
        }
    movie.Reset();
    CHECK(movie.Info().frames == 0 && movie.Audio().pcm.empty() && !movie.DecodeFrame(0, frame));
    std::puts("AVI: Video 1 colors/deltas, palette changes, PCM bytes, fractional/nonzero timeline, owned input, seeks "
              "and bounded failure passed.");
}
