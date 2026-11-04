/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

static int Fail(int line, KauaiSDL::Host &host)
{
    std::fprintf(stderr, "SDL host check failed at line %d: %s\n", line, host.Error());
    return 1;
}
static bool Next(KauaiSDL::Host &host, KauaiSDL::EventKind kind, KauaiSDL::Event &event)
{
    for (unsigned i = 0; i < 100; ++i)
        if (host.Poll(event, 1) && event.kind == kind)
            return true;
    return false;
}
static bool CheckPixels(KauaiSDL::Host &host, const char *path, const std::vector<std::uint32_t> &pixels)
{
    KauaiSDL::PresentationStatus view;
    if (!host.Presentation(view) || !host.SaveBMP(path))
        return false;
    auto image = SDL_LoadBMP(path);
    if (!image)
        return false;
    const int scale = std::min(view.outputWidth / 320, view.outputHeight / 240);
    // Readback covers SDL's presentation viewport, excluding the letterbox.
    bool matches = image->w == view.width && image->h == view.height;
    for (int y = 0; matches && y < image->h; ++y)
        for (int x = 0; matches && x < image->w; ++x)
        {
            const auto expected = pixels[y / scale * 320 + x / scale] & 0xFFFFFF;
            Uint8 r, g, b;
            matches = SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, nullptr) &&
                      ((std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b) == expected;
            if (!matches)
                std::fprintf(stderr, "Rendered pixel mismatch at %d,%d in %dx%d output\n", x, y, image->w, image->h);
        }
    SDL_DestroySurface(image);
    return matches;
}
int main(int argc, char **argv)
{
    const bool scalingOnly = argc == 3 && std::strcmp(argv[2], "--scaling-only") == 0;
    if (argc != 2 && !scalingOnly)
        return 2;
    KauaiSDL::Host host;
    if (!host.Create("Kauai SDL host check", 320, 240, true))
    {
        std::fprintf(stderr, "%s\n", host.Error());
        return Fail(__LINE__, host);
    }
    std::vector<std::uint32_t> pixels(320 * 240);
    KauaiPixels::DamageRect damage;
    if (!host.DamageBounds(damage) || damage.left || damage.top || damage.right != 320 || damage.bottom != 240)
        return Fail(__LINE__, host);
    host.Validate();
    if (host.DamageBounds(damage))
        return Fail(__LINE__, host);
    KauaiPixels::DamageRect dirty{4, 5, 8, 9}, area{0, 0, 32, 32};
    host.Invalidate(&dirty);
    if (!host.ScrollDamage(area, 1, 2) || !host.DamageBounds(damage) || damage.left != 5 || damage.top != 7 ||
        damage.right != 9 || damage.bottom != 11)
        return Fail(__LINE__, host);
    std::vector<KauaiPixels::DamageRect> taken;
    if (!host.TakeDamage(taken) || taken.empty() || host.DamageBounds(damage))
        return Fail(__LINE__, host);
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
            pixels[y * 320 + x] = ((x & 255) << 16) | ((y & 255) << 8) | 127;
    host.BindPixels(pixels.data(), 320 * 4);
    if (!host.Present(true) || !host.SaveBMP(argv[1]))
        return Fail(__LINE__, host);
    // This mode also runs against the real display renderer without depending
    // on clipboard ownership or foreground input in a hidden test window.
    if (scalingOnly)
    {
        for (const auto &dimensions : {std::pair<int, int>{850, 510}, {1000, 700}, {1001, 721}, {321, 241}})
        {
            if (!host.SetWindowSize(dimensions.first, dimensions.second) || !host.Present(true) ||
                !CheckPixels(host, argv[1], pixels))
                return Fail(__LINE__, host);
            int width = 0, height = 0;
            if (!host.WindowSize(width, height) || width != dimensions.first || height != dimensions.second ||
                host.Width() != 320 || host.Height() != 240)
                return Fail(__LINE__, host);
        }
        std::puts("SDL presentation: every rendered pixel matches integer nearest-neighbor scaling.");
        return 0;
    }
    KauaiSDL::Event event;
    while (host.Poll(event))
    {
    }
    // CTest uses SDL's dummy video backend, so this is an isolated clipboard.
    std::string clipboard = "Caf\xC3\xA9\n\xF0\x9F\x98\x80";
    if (!host.SetClipboardText(clipboard) || !host.OwnsClipboard())
        return Fail(__LINE__, host);
    std::string retrieved;
    if (!host.GetClipboardText(retrieved) || retrieved != clipboard ||
        !Next(host, KauaiSDL::EventKind::clipboard, event) || !event.active)
        return Fail(__LINE__, host);
    if (host.SetClipboardText(std::string("a\0b", 3)) || !host.GetClipboardText(retrieved) || retrieved != clipboard)
        return Fail(__LINE__, host);
    if (!host.SetClipboardText("") || !host.GetClipboardText(retrieved) || !retrieved.empty())
        return Fail(__LINE__, host);
    const std::uint8_t andBits[] = {0x55, 77, 0xAA, 77}, xorBits[] = {0x33, 88, 0xCC, 88};
    const auto cursor = host.CreateCursor(andBits, xorBits, 8, 2, 2, 1, 1);
    if (!cursor || !host.SetCursor(cursor) || host.Cursors().live != 1 || host.Cursors().current != cursor ||
        host.CreateCursor(andBits, xorBits, 8, 2, 2, 8, 1))
        return Fail(__LINE__, host);
    if (!host.SetCursorVisible(false) || host.Cursors().visible || !host.SetCursorVisible(true) ||
        !host.Cursors().visible || !host.PushPointer(0, 0, false) ||
        !Next(host, KauaiSDL::EventKind::buttonUp, event) ||
        !host.SaveBMP((std::string(argv[1]) + ".cursor.bmp").c_str(), true))
        return Fail(__LINE__, host);
    std::vector<std::uint32_t> unchanged;
    int capturedWidth, capturedHeight;
    if (!host.CopyPixels(unchanged, capturedWidth, capturedHeight) || unchanged != pixels)
        return Fail(__LINE__, host);
    host.DestroyCursor(cursor);
    if (host.SetCursor(cursor) || host.Cursors().current || host.Cursors().live)
        return Fail(__LINE__, host);
    if (!host.PushPointer(123, 45, true) || !host.PushPointer(123, 45, false) ||
        !Next(host, KauaiSDL::EventKind::buttonDown, event))
        return Fail(__LINE__, host);
    float x, y;
    bool down;
    host.Mouse(x, y, down);
    if (!down || x != 123 || y != 45)
        return Fail(__LINE__, host);
    if (!Next(host, KauaiSDL::EventKind::buttonUp, event))
        return Fail(__LINE__, host);
    host.Mouse(x, y, down);
    if (down)
        return Fail(__LINE__, host);
    if (!host.WarpPointer(160, 120))
        return Fail(__LINE__, host);
    host.Mouse(x, y, down);
    if (x != 160 || y != 120 || down)
        return Fail(__LINE__, host);
    // A wider window adds side letterboxing. Queued logical clicks must still
    // reach exactly the same Kauai control after SDL's event conversion.
    if (!host.SetWindowSize(800, 480) || !host.Present(true))
        return Fail(__LINE__, host);
    if (!host.WarpPointer(160, 120))
        return Fail(__LINE__, host);
    host.Mouse(x, y, down);
    if (x != 160 || y != 120 || down)
        return Fail(__LINE__, host);
    if (!host.PushPointer(123, 45, true) || !Next(host, KauaiSDL::EventKind::buttonDown, event))
        return Fail(__LINE__, host);
    host.Mouse(x, y, down);
    if (!down || x != 123 || y != 45)
        return Fail(__LINE__, host);
    if (!host.PushPointer(123, 45, false) || !Next(host, KauaiSDL::EventKind::buttonUp, event))
        return Fail(__LINE__, host);
    // Sizes between integer multiples keep a uniform pixel grid and hit testing.
    for (const auto &dimensions : {std::pair<int, int>{850, 510}, {1000, 700}, {1001, 721}, {321, 241}, {160, 120}})
    {
        if (!host.SetWindowSize(dimensions.first, dimensions.second) || !host.Present(true))
            return Fail(__LINE__, host);
        KauaiSDL::PresentationStatus presentation;
        if (!host.Presentation(presentation))
            return Fail(__LINE__, host);
        const int scale = std::min(presentation.outputWidth / 320, presentation.outputHeight / 240);
        if (scale < 1 || presentation.width != 320 * scale || presentation.height != 240 * scale ||
            presentation.x != (presentation.outputWidth - presentation.width) / 2.0f ||
            presentation.y != (presentation.outputHeight - presentation.height) / 2.0f)
            return Fail(__LINE__, host);
        // Check every rendered pixel, not just SDL's reported rectangle.
        if (!CheckPixels(host, argv[1], pixels))
            return Fail(__LINE__, host);
        if (!host.PushPointer(123, 45, true) || !Next(host, KauaiSDL::EventKind::buttonDown, event) || event.x != 123 ||
            event.y != 45 || !host.PushPointer(123, 45, false) || !Next(host, KauaiSDL::EventKind::buttonUp, event))
            return Fail(__LINE__, host);
    }
    // Fullscreen must fill the display even when its size is not an integer
    // multiple of the canvas. Exercise both app and window-manager requests.
    auto window = SDL_GetWindowFromID(host.WindowID());
    for (bool external : {false, true})
    {
        if (!(external ? SDL_SetWindowFullscreen(window, true) : host.SetFullscreen(true)) || !SDL_SyncWindow(window))
            return Fail(__LINE__, host);
        while (host.Poll(event))
        {
        }
        KauaiSDL::PresentationStatus view;
        if (!host.Present(true) || !host.Presentation(view))
            return Fail(__LINE__, host);
        const float scale = std::min(view.outputWidth / 320.0f, view.outputHeight / 240.0f);
        if (std::abs(view.width - 320 * scale) >= 1 || std::abs(view.height - 240 * scale) >= 1 ||
            std::abs(view.x - (view.outputWidth - view.width) / 2) > 0.001f ||
            std::abs(view.y - (view.outputHeight - view.height) / 2) > 0.001f || !host.SaveBMP(argv[1]))
            return Fail(__LINE__, host);
        auto image = SDL_LoadBMP(argv[1]);
        if (!image || std::abs(image->w - view.width) >= 1 || std::abs(image->h - view.height) >= 1)
            return Fail(__LINE__, host);
        // Sample centers of source pixels to avoid nearest-neighbor boundaries.
        for (const auto &point : {std::pair<int, int>{0, 0}, {160, 120}, {319, 239}})
        {
            const int x = std::min(image->w - 1, int((point.first + 0.5f) * view.width / 320));
            const int y = std::min(image->h - 1, int((point.second + 0.5f) * view.height / 240));
            Uint8 r, g, b;
            if (!SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, nullptr) ||
                ((std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b) !=
                    (pixels[point.second * 320 + point.first] & 0xFFFFFF))
                return Fail(__LINE__, host);
        }
        SDL_DestroySurface(image);
        if (!host.SetIntegerScaling(true) || !host.IntegerScaling() || !host.Present(true) ||
            !CheckPixels(host, argv[1], pixels))
            return Fail(__LINE__, host);
        if (!host.SetIntegerScaling(false) || host.IntegerScaling() || !host.Present(true) || !host.Presentation(view) ||
            std::abs(view.width - 320 * scale) >= 1 || std::abs(view.height - 240 * scale) >= 1)
            return Fail(__LINE__, host);
        if (!host.PushPointer(123, 45, true) || !Next(host, KauaiSDL::EventKind::buttonDown, event) ||
            std::abs(event.x - 123) > 0.001f || std::abs(event.y - 45) > 0.001f ||
            !host.PushPointer(123, 45, false) || !Next(host, KauaiSDL::EventKind::buttonUp, event))
            return Fail(__LINE__, host);
        if (!(external ? SDL_SetWindowFullscreen(window, false) : host.SetFullscreen(false)) || !SDL_SyncWindow(window))
            return Fail(__LINE__, host);
        while (host.Poll(event))
        {
        }
        if (!host.Present(true) || !CheckPixels(host, argv[1], pixels))
            return Fail(__LINE__, host);
    }
    for (auto key : {65u, 27u, 39u})
    {
        if (!host.PushKey(key, true) || !Next(host, KauaiSDL::EventKind::keyDown, event) || event.key != key)
            return Fail(__LINE__, host);
    }
    std::string typed = "Caf\xC3\xA9";
    if (!host.PushText(typed))
        return Fail(__LINE__, host);
    typed.assign(1000, 'X'); // Queue owns its text after caller mutation.
    if (!Next(host, KauaiSDL::EventKind::text, event) || event.text != "Caf\xC3\xA9")
        return Fail(__LINE__, host);
    if (!host.PushText("discard") || !host.PushText("also discard"))
        return Fail(__LINE__, host);
    host.FlushInput(false, true);
    if (Next(host, KauaiSDL::EventKind::text, event))
        return Fail(__LINE__, host);
    host.PushPointer(0, 0, true);
    host.PushKey(66, true);
    host.FlushInput(true, false);
    if (!Next(host, KauaiSDL::EventKind::keyDown, event) || event.key != 66)
        return Fail(__LINE__, host);
    host.UnbindPixels(pixels.data());
    if (host.Present(true))
        return Fail(__LINE__, host);
    if (!host.PushText("close before polling"))
        return Fail(__LINE__, host);
    host.Close();
    if (host.Active() || !host.Create("Restart", 64, 64, true))
        return Fail(__LINE__, host);
    if (!host.DamageBounds(damage) || damage.left || damage.top || damage.right != 64 || damage.bottom != 64)
        return Fail(__LINE__, host);
    const auto freshCursor = host.CreateCursor(andBits, xorBits, 8, 2, 2, 1, 1);
    if (!freshCursor || freshCursor == cursor)
        return Fail(__LINE__, host);
    host.DestroyCursor(cursor);
    if (!host.SetCursor(freshCursor) || host.Cursors().live != 1)
        return Fail(__LINE__, host);
    if (Next(host, KauaiSDL::EventKind::text, event))
        return Fail(__LINE__, host);
    std::puts("SDL host: framebuffer presentation/capture, queued pointer/key state, input flush and restart passed.");
    return 0;
}
