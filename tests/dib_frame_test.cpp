/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dib_frame.h"
#include "dib_frame_fixture.h"
#include "pixel_canvas.h"
#include <cstdio>
#include <cstdlib>
#include "kauai_core.h"
#undef size
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error DIB frame decoding must not import the Windows SDK
#endif
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *, LegacyLong, schar *, void *, LegacyLong)
{
    std::exit(1);
}
static void Check(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "DIB frame failed at %d\n", line);
        std::exit(1);
    }
}
#define CHECK(value) Check(value, __LINE__)
int main()
{
    unsigned rejected = 0;
    for (int depth : {24, 32})
        for (bool bottomUp : {false, true})
        {
            auto bytes = MakeDib(7, 5, depth, bottomUp);
            const auto untouched = bytes;
            KauaiPixels::DibFrame frame;
            std::size_t extent = 3;
            CHECK(KauaiPixels::DibFrameBytes(bytes.data(), 40, extent) && extent == bytes.size());
            CHECK(frame.Read(bytes.data(), bytes.size()));
            CHECK(frame.width == 7 && frame.height == 5 && frame.pitch == 28);
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 7; ++x)
                {
                    auto pixel = frame.pixels.data() + y * frame.pitch + x * 4;
                    CHECK(pixel[0] == x * 13 + y * 7 && pixel[1] == static_cast<std::uint8_t>(x * 31 + y * 3) &&
                          pixel[2] == x * 5 + y * 19 && pixel[3] == (depth == 32 ? 0x91 : 0));
                }
            const auto prior = frame.pixels;
            const RC bounds(0, 0, frame.width, frame.height);
            std::vector<std::uint8_t> drawn(prior.size(), 0xA5);
            CHECK(KauaiPixels::DrawDibFrame({drawn.data(), frame.width, frame.height, frame.pitch}, frame, bounds));
            CHECK(drawn == prior);
            drawn.assign(3 * 2 * 4, 0xA5);
            CHECK(KauaiPixels::DrawDibFrame({drawn.data(), 3, 2, 12}, frame, RC(0, 0, 3, 2)));
            const int xs24[] = {0, 3, 5}, xs32[] = {1, 3, 5};
            const int ys24[] = {1, 4}, ys32[] = {1, 3};
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 3; ++x)
                {
                    const auto source = prior.data() + (depth == 24 ? ys24[y] : ys32[y]) * frame.pitch +
                                        (depth == 24 ? xs24[x] : xs32[x]) * 4;
                    for (int byte = 0; byte < 4; ++byte)
                        CHECK(drawn[(y * 3 + x) * 4 + byte] == source[byte]);
                }
            CHECK(!KauaiPixels::DrawDibFrame({frame.pixels.data(), frame.width, frame.height, frame.pitch}, frame,
                                             bounds));
            CHECK(frame.pixels == prior);
            for (std::size_t n = 0; n < bytes.size(); ++n)
            {
                CHECK(!frame.Read(bytes.data(), n) && frame.pixels == prior && frame.width == 7);
                ++rejected;
            }
            for (const auto &field : {std::pair<unsigned, std::uint32_t>{0, 108},
                                      {4, 0},
                                      {8, 0x80000000},
                                      {12, 0},
                                      {14, 16},
                                      {16, 1},
                                      {20, 1},
                                      {32, 1}})
            {
                auto bad = bytes;
                PutDib32(bad, field.first, field.second);
                CHECK(!frame.Read(bad.data(), bad.size()) && frame.pixels == prior);
                extent = 3;
                CHECK(!KauaiPixels::DibFrameBytes(bad.data(), 40, extent) && extent == 3);
            }
            auto bad = bytes;
            PutDib32(bad, 4, INT32_MAX);
            CHECK(!frame.Read(bad.data(), bad.size()) && frame.pixels == prior);
            CHECK(!frame.Read(nullptr, 100) && frame.pixels == prior && bytes == untouched);
        }
    std::printf("DIB frames: top/bottom orientation, BGR/BGRX pixels, padding, overflow and %u transactional "
                "truncation rejections passed.\n",
                rejected);
}
