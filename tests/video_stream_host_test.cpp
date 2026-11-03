/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "video_stream_host.h"
#include <cstdio>
#include <memory>
#include <vector>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Video host failed at %d\n", __LINE__);                                               \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    static ApplicationBase app;
#ifdef KAUAI_SDL3
    _putenv_s("KAUAI_FONT_PROVIDER", "freetype");
#else
    vwig.hdcApp = GetDC(nullptr);
    CHECK(vwig.hdcApp);
#endif
    CHECK(vntl.FInit());
    String path;
    path.SetSzs(argv[1]);
    Filename filename;
    CHECK(filename.FBuildFromPath(&path));
    AVIFileInit();
    PAVIFILE writer = nullptr;
    CHECK(AVIFileOpen(&writer, path.Psz(), OF_CREATE | OF_WRITE, nullptr) == 0);
    AVISTREAMINFO description = {};
    description.fccType = streamtypeVIDEO;
    description.dwScale = 1;
    description.dwRate = 25;
    description.dwStart = 3;
    SetRect(&description.rcFrame, 0, 0, 16, 8);
    PAVISTREAM stream = nullptr;
    CHECK(AVIFileCreateStream(writer, &stream, &description) == 0);
    BITMAPINFOHEADER format = {};
    format.biSize = sizeof(format);
    format.biWidth = 16;
    format.biHeight = 8;
    format.biPlanes = 1;
    format.biBitCount = 24;
    format.biSizeImage = 16 * 8 * 3;
    CHECK(AVIStreamSetFormat(stream, 3, &format, sizeof(format)) == 0);
    std::vector<byte> pixels(format.biSizeImage);
    for (int frame = 0; frame < 3; ++frame)
    {
        for (std::size_t i = 0; i < pixels.size(); i += 3)
        {
            pixels[i] = static_cast<byte>(17 + frame * 50);
            pixels[i + 1] = 38;
            pixels[i + 2] = 99;
        }
        CHECK(AVIStreamWrite(stream, 3 + frame, 1, pixels.data(), static_cast<LONG>(pixels.size()), AVIIF_KEYFRAME,
                             nullptr, nullptr) == 0);
    }
    AVIStreamRelease(stream);
    AVIFileRelease(writer);
    PAVIFILE oracle = nullptr;
    CHECK(AVIFileOpen(&oracle, path.Psz(), OF_READ | OF_SHARE_DENY_WRITE, nullptr) == 0);
    stream = nullptr;
    CHECK(AVIFileGetStream(oracle, &stream, streamtypeVIDEO, 0) == 0);
    const auto start = AVIStreamStart(stream), length = AVIStreamLength(stream);
    CHECK(start == 3 && length == 3);
    std::printf("Native AVI fixture: first sample %ld, length %ld.\n", start, length);
    std::unique_ptr<KauaiVideo::StreamHost> host(KauaiVideo::NewStreamHost());
    CHECK(host && host->Open(&filename));
    const auto info = host->Info();
    CHECK(info.width == 16 && info.height == 8 && info.frames == length && length >= 3);
    for (LONG frame = 0; frame < length; ++frame)
        CHECK(host->TimeForFrame(frame) == AVIStreamSampleToTime(stream, frame + start));
    for (unsigned milliseconds : {0u, 1u, 39u, 40u, 79u, 80u, 120u, 1000u, 0xFFFFFFFFu})
        CHECK(host->FrameForTime(milliseconds) ==
              AVIStreamTimeToSample(stream, static_cast<LONG>(milliseconds)) - start);
    RC rectangle(0, 0, 16, 8);
    auto port = GraphicsPort::PgptNewOffscreen(&rectangle, 32);
    CHECK(port);
    {
        GraphicsEnvironment environment(port);
        // AVI keeps any leading stream samples in its native metadata. Read
        // the three written frames and verify their decoded pixels via the
        // actual GraphicsEnvironment / SDL graphics adapter.
        for (int frame = 0; frame < 3; ++frame)
        {
            const auto relative = 3 + frame - start;
            CHECK(host->DrawFrame(relative, &environment, &rectangle));
            auto decoded = reinterpret_cast<const std::uint32_t *>(port->PrgbLockPixels());
            const auto expected = std::uint32_t(17 + frame * 50) | (38u << 8) | (99u << 16);
            for (int pixel = 0; pixel < 16 * 8; ++pixel)
                CHECK((decoded[pixel] & 0xFFFFFF) == expected);
            port->Unlock();
        }
        CHECK(!host->DrawFrame(-1, &environment, &rectangle) &&
              !host->DrawFrame(info.frames, &environment, &rectangle));
    }
    ReleasePpo(&port);
    AVIStreamRelease(stream);
    AVIFileRelease(oracle);
    CHECK(!host->Open(nullptr) && host->Info().frames == 0);
    CHECK(host->TimeForFrame(0) == -1 && host->FrameForTime(0) == -1);
    String missing = path;
    CHECK(missing.FAppendSz(PszLit("-missing.avi")));
    Filename missingFile;
    CHECK(missingFile.FBuildFromPath(&missing) && !host->Open(&missingFile) && host->Info().frames == 0);
    CHECK(host->Open(&filename)); // Reopening after failure owns fresh handles.
    host.reset();
    CHECK(filename.FDelete());
    AVIFileExit();
#ifndef KAUAI_SDL3
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
#endif
    std::puts("Video host: real AVI source, metadata/offset/timing parity, decoded frame pixels, invalid indices, "
              "failed open, reopen and released file handles passed.");
}
