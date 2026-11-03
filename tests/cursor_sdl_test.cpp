/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <vector>
#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "SDL cursor check failed at %d: %s\n", __LINE__, session.host.Error());               \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
struct Session
{
    KauaiSDL::Host host;
};
static void Pump(KauaiSDL::Host &host)
{
    KauaiSDL::Event event;
    for (int i = 0; i < 30; ++i)
        host.Poll(event, 1);
}
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    Session session;
    auto &host = session.host;
    CHECK(host.Create("SDL cursor compatibility check", 320, 240, true));
    Pump(host);
    auto dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 72;
    info.bmiHeader.biHeight = -40;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    CHECK(dc && bitmap && bits);
    auto previous = SelectObject(dc, bitmap);
    std::vector<std::uint32_t> framebuffer(320 * 240);
    host.BindPixels(framebuffer.data(), 320 * 4);
    std::uint64_t stale = 0;
    for (int width : {16, 32})
    {
        const int pitch = width / 8;
        std::vector<byte> andBits(pitch * width), xorBits(pitch * width);
        for (std::size_t i = 0; i < andBits.size(); ++i)
        {
            andBits[i] = static_cast<byte>(0x55 ^ (i * 13));
            xorBits[i] = static_cast<byte>(0x33 ^ (i * 37));
        }
        auto id = host.CreateCursor(andBits.data(), xorBits.data(), width, width, pitch, 3, 4);
        CHECK(id && host.SetCursor(id));
        Pump(host);
        auto reference = CreateCursor(GetModuleHandle(nullptr), 3, 4, width, width, andBits.data(), xorBits.data());
        CHECK(reference);
        auto pixels = static_cast<std::uint32_t *>(bits);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 72; ++x)
                pixels[y * 72 + x] = 0x123456 ^ (y * 911 + (x % 36) * 37);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 320; ++x)
                framebuffer[y * 320 + x] = 0x123456 ^ (y * 911 + (x % 36) * 37);
        CHECK(DrawIconEx(dc, 2, 2, reference, width, width, 0, nullptr, DI_NORMAL));
        CHECK(host.PushPointer(41, 6, false)); // Hotspot (3,4) places top left at (38,2).
        Pump(host);
        CHECK(host.SaveBMP(argv[1], true));
        std::ifstream capture(argv[1], std::ios::binary | std::ios::ate);
        CHECK(capture);
        std::vector<unsigned char> saved(static_cast<std::size_t>(capture.tellg()));
        capture.seekg(0);
        CHECK(capture.read(reinterpret_cast<char *>(saved.data()), saved.size()));
        const auto read32 = [&](std::size_t offset) {
            return std::uint32_t(saved[offset]) | (std::uint32_t(saved[offset + 1]) << 8) |
                   (std::uint32_t(saved[offset + 2]) << 16) | (std::uint32_t(saved[offset + 3]) << 24);
        };
        CHECK(saved.size() > 54 && read32(18) == 320 && (saved[28] == 24 || saved[28] == 32));
        const auto bytesPerPixel = saved[28] / 8;
        const auto rowBytes = (320 * bytesPerPixel + 3) & ~3;
        const auto offset = read32(10);
        const auto savedHeight = static_cast<std::int32_t>(read32(22));
        CHECK((savedHeight == 240 || savedHeight == -240) && offset + rowBytes * 240 <= saved.size());
        std::vector<std::uint32_t> rendered(320 * 240);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 320; ++x)
            {
                const auto pixel = offset + (savedHeight > 0 ? 239 - y : y) * rowBytes + x * bytesPerPixel;
                rendered[y * 320 + x] = std::uint32_t(saved[pixel]) | (std::uint32_t(saved[pixel + 1]) << 8) |
                                        (std::uint32_t(saved[pixel + 2]) << 16);
            }
        GdiFlush();
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 36; ++x)
                if ((pixels[y * 72 + x] & 0xFFFFFF) != (rendered[y * 320 + x + 36] & 0xFFFFFF))
                {
                    std::fprintf(stderr, "Cursor raster mismatch: size=%d at=%d,%d expected=%06x actual=%06x\n", width,
                                 x, y, pixels[y * 72 + x] & 0xFFFFFF, rendered[y * 320 + x + 36] & 0xFFFFFF);
                    return 1;
                }
        DestroyCursor(reference);
        CHECK(host.Cursors().current == id && host.Cursors().live == 1);
        CHECK(host.SetCursorVisible(false) && !host.Cursors().visible);
        CHECK(host.SetCursorVisible(true) && host.Cursors().visible);
        host.DestroyCursor(id);
        CHECK(!host.SetCursor(id) && host.Cursors().live == 0);
        stale = id;
    }
    // Dummy video has no native arrow/wait cursors. Unsupported system
    // selections must still leave the custom software layer disabled.
    host.SetSystemCursor(true);
    CHECK(!host.Cursors().current);
    host.SetSystemCursor(false);
    CHECK(!host.Cursors().current);
    CHECK(!host.CreateCursor(nullptr, nullptr, 32, 32, 4, 0, 0));
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    host.Close();
    CHECK(host.Create("SDL cursor restart check", 64, 64, true));
    byte plane[128] = {};
    auto fresh = host.CreateCursor(plane, plane, 32, 32, 4, 0, 0);
    CHECK(fresh && fresh != stale);
    host.DestroyCursor(stale);
    CHECK(host.SetCursor(fresh) && host.Cursors().live == 1);
    std::puts(
        "SDL cursors: 16/32 pixel GDI raster/hotspot comparisons, inversion, visibility, lifetime and restart passed.");
    return 0;
}
