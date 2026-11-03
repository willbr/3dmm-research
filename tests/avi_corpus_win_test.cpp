/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "avi_movie.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <windows.h>
#include <vfw.h>
#define CHECK(v)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(v))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "AVI oracle failed at %d: %s (%s)\n", __LINE__, #v, path.string().c_str());           \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    AVIFileInit();
    unsigned files = 0, total = 0;
    for (const auto &entry : std::filesystem::directory_iterator(argv[1]))
    {
        if (entry.path().extension() != ".avi")
            continue;
        const auto path = entry.path();
        std::ifstream input(path, std::ios::binary);
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
        KauaiVideo::AviMovie movie;
        CHECK(movie.Read(bytes.data(), bytes.size()));
        PAVIFILE file = nullptr;
        PAVISTREAM stream = nullptr;
        CHECK(AVIFileOpenW(&file, path.c_str(), OF_READ, nullptr) == 0);
        CHECK(AVIFileGetStream(file, &stream, streamtypeVIDEO, 0) == 0);
        const auto start = AVIStreamStart(stream), length = AVIStreamLength(stream);
        CHECK(start == static_cast<LONG>(movie.Info().start) && length == movie.Info().frames);
        auto decoder = AVIStreamGetFrameOpen(stream, nullptr);
        CHECK(decoder);
        KauaiPixels::DibFrame frame;
        auto compare = [&](LONG index) {
            auto header = static_cast<BITMAPINFOHEADER *>(AVIStreamGetFrame(decoder, start + index));
            if (!header || !movie.DecodeFrame(index, frame) || header->biWidth != frame.width ||
                header->biHeight != frame.height || header->biBitCount != 8)
                return false;
            const auto palette =
                reinterpret_cast<const RGBQUAD *>(reinterpret_cast<const std::uint8_t *>(header) + header->biSize);
            const auto native =
                reinterpret_cast<const std::uint8_t *>(palette + (header->biClrUsed ? header->biClrUsed : 256));
            const auto pitch = (frame.width + 3) & ~3;
            for (int y = 0; y < frame.height; ++y)
                for (int x = 0; x < frame.width; ++x)
                {
                    const auto &color = palette[native[(frame.height - 1 - y) * pitch + x]];
                    const auto p = frame.pixels.data() + y * frame.pitch + x * 4;
                    if (p[0] != color.rgbBlue || p[1] != color.rgbGreen || p[2] != color.rgbRed || p[3])
                    {
                        std::fprintf(stderr, "Pixel mismatch frame %ld at %d,%d\n", index, x, y);
                        return false;
                    }
                }
            return true;
        };
        for (LONG i = 0; i < length; ++i)
        {
            CHECK(movie.TimeForFrame(i) == AVIStreamSampleToTime(stream, start + i));
            CHECK(compare(i));
            ++total;
        }
        CHECK(compare(0) && compare(length / 2) && compare(length - 1));
        for (LONG ms = -1000; ms < movie.TimeForFrame(length) + 1000; ms += 37)
            CHECK(movie.FrameForTime(ms) == AVIStreamTimeToSample(stream, ms) - start);
        AVIStreamGetFrameClose(decoder);
        AVIStreamRelease(stream);
        CHECK(AVIFileGetStream(file, &stream, streamtypeAUDIO, 0) == 0);
        LONG count = 0, samples = 0;
        CHECK(AVIStreamRead(stream, AVIStreamStart(stream), AVIStreamLength(stream), nullptr, 0, &count, &samples) ==
              0);
        std::vector<std::uint8_t> pcm(count);
        CHECK(AVIStreamRead(stream, AVIStreamStart(stream), AVIStreamLength(stream), pcm.data(), count, &count,
                            &samples) == 0);
        pcm.resize(count);
        CHECK(pcm == movie.Audio().pcm);
        AVIStreamRelease(stream);
        AVIFileRelease(file);
        ++files;
        std::printf("%s: %ld frames and %ld PCM bytes match native decoder.\n", path.filename().string().c_str(),
                    length, count);
    }
    AVIFileExit();
    if (!files)
        return 3;
    std::printf("AVI native corpus: %u files, %u frames, every decoded pixel, seeks, timeline and PCM bytes passed.\n",
                files, total);
}
