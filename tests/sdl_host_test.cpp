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
    bool matches = scale >= 1 && view.width == 320 * scale && view.height == 240 * scale &&
                   view.x == (view.outputWidth - view.width) / 2 && view.y == (view.outputHeight - view.height) / 2 &&
                   image->w == view.width && image->h == view.height;
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
static bool CheckFittedPixels(KauaiSDL::Host &host, const char *path, const std::vector<std::uint32_t> &pixels)
{
    KauaiSDL::PresentationStatus view;
    if (!host.Presentation(view) || !host.SaveBMP(path))
        return false;
    const float scale = std::min(view.outputWidth / 320.0f, view.outputHeight / 240.0f);
    if (std::abs(view.width - 320 * scale) >= 1 || std::abs(view.height - 240 * scale) >= 1 ||
        std::abs(view.x - (view.outputWidth - view.width) / 2) > 0.001f ||
        std::abs(view.y - (view.outputHeight - view.height) / 2) > 0.001f)
        return false;
    auto image = SDL_LoadBMP(path);
    if (!image)
        return false;
    bool matches = std::abs(image->w - view.width) < 1 && std::abs(image->h - view.height) < 1;
    // Sample source-pixel centers, away from fractional nearest-neighbor boundaries.
    for (const auto &point : {std::pair<int, int>{0, 0}, {160, 120}, {319, 239}})
    {
        const int x = std::min(image->w - 1, int((point.first + 0.5f) * view.width / 320));
        const int y = std::min(image->h - 1, int((point.second + 0.5f) * view.height / 240));
        Uint8 r, g, b;
        matches = matches && SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, nullptr) &&
                  ((std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b) ==
                      (pixels[point.second * 320 + point.first] & 0xFFFFFF);
    }
    SDL_DestroySurface(image);
    return matches;
}
// Standalone hardware/software presentation check; it never touches clipboard
// or desktop input. The full host workflow below uses an isolated dummy driver.
static int IndexedPresentation(const char *path, KauaiSDL::Host &host)
{
    std::vector<std::uint32_t> canvas(320 * 240), copied;
    std::vector<std::uint8_t> indices(644 * 480, 0xEE);
    Color palette[256];
    host.BindPixels(canvas.data(), 320 * 4);
    for (unsigned frame = 1; frame <= 3; ++frame)
    {
        for (int i = 0; i < 256; ++i)
            palette[i] = {std::uint8_t(i * 17), std::uint8_t(255 - i), std::uint8_t(i + frame * 23), 0};
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x)
                indices[y * 644 + x] = std::uint8_t(x / 2 + y / 2 + (frame == 1 ? 0 : 5));
        int width, height;
        if (!host.SetIndexedPresentationPixels(indices.data(), 640, 480, 644, palette, true, frame == 1 ? 1 : 2) ||
            !host.Present(true) || !host.Present(true) || !host.SaveBMP(path, false) ||
            !host.CopyPresentationPixels(copied, width, height) || width != 640 || height != 480)
            return Fail(__LINE__, host);
        auto image = SDL_LoadBMP(path);
        if (!image)
            return Fail(__LINE__, host);
        for (const auto &point : {std::pair<int, int>{0, 0}, {159, 119}, {319, 239}, {17, 203}})
        {
            const auto color = palette[indices[point.second * 2 * 644 + point.first * 2]];
            const auto expected = (std::uint32_t(color.bRed) << 16) | (std::uint32_t(color.bGreen) << 8) | color.bBlue;
            Uint8 r, g, b;
            if (!SDL_ReadSurfacePixel(image, int((point.first + 0.5) * image->w / 320),
                                      int((point.second + 0.5) * image->h / 240), &r, &g, &b, nullptr) ||
                ((std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b) != expected ||
                copied[point.second * 2 * 640 + point.first * 2] != expected)
            {
                SDL_DestroySurface(image);
                return Fail(__LINE__, host);
            }
        }
        SDL_DestroySurface(image);
    }
    host.SetPresentationPixels(nullptr);
    host.UnbindPixels(canvas.data());
    std::puts("SDL indexed presentation: padded frames, palette changes and texture reuse passed.");
    return 0;
}
int main(int argc, char **argv)
{
    const bool scalingOnly = argc == 3 && std::strcmp(argv[2], "--scaling-only") == 0;
    const bool indexedOnly = argc == 3 && std::strcmp(argv[2], "--indexed-only") == 0;
    if (argc != 2 && !scalingOnly && !indexedOnly)
        return 2;
    KauaiSDL::Host host;
    if (!host.Create("Kauai SDL host check", 320, 240, true))
    {
        std::fprintf(stderr, "%s\n", host.Error());
        return Fail(__LINE__, host);
    }
    if (indexedOnly)
        return IndexedPresentation(argv[1], host);
    if (std::strcmp(SDL_GetCurrentVideoDriver(), "dummy") == 0)
    {
        host.ResumeAfterModal();
        KauaiSDL::WindowStatus state;
        KauaiSDL::Event event;
        if (!host.WindowState(state) || !state.keyboardFocus || !Next(host, KauaiSDL::EventKind::focus, event) ||
            !event.active)
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
    if (host.IntegerScaling() || !CheckFittedPixels(host, argv[1], pixels))
        return Fail(__LINE__, host);
    // This mode also runs against the real display renderer without depending
    // on clipboard ownership or foreground input in a hidden test window.
    if (scalingOnly)
    {
        for (const auto &dimensions : {std::pair<int, int>{850, 510}, {1000, 700}, {1001, 721}, {321, 241}})
        {
            if (!host.SetWindowSize(dimensions.first, dimensions.second))
                return Fail(__LINE__, host);
            for (bool integer : {true, false})
                if (!host.SetIntegerScaling(integer) || !host.Present(true) ||
                    !(integer ? CheckPixels(host, argv[1], pixels) : CheckFittedPixels(host, argv[1], pixels)))
                    return Fail(__LINE__, host);
            int width = 0, height = 0;
            if (!host.WindowSize(width, height) || width != dimensions.first || height != dimensions.second ||
                host.Width() != 320 || host.Height() != 240)
                return Fail(__LINE__, host);
        }
        // Exercise a real window-manager maximize, including changing the preference
        // while maximized. The dummy driver does not provide maximization.
        auto window = SDL_GetWindowFromID(host.WindowID());
        if (!host.EnsureInteractive() || !host.SetMaximized(true) || !SDL_SyncWindow(window))
            return Fail(__LINE__, host);
        KauaiSDL::Event event;
        while (host.Poll(event))
        {
        }
        KauaiSDL::WindowStatus state;
        if (!host.WindowState(state) || !state.maximized)
            return Fail(__LINE__, host);
        for (bool integer : {true, false})
            if (!host.SetIntegerScaling(integer) || !host.Present(true) ||
                !(integer ? CheckPixels(host, argv[1], pixels) : CheckFittedPixels(host, argv[1], pixels)))
                return Fail(__LINE__, host);
        std::puts("SDL presentation: fitted and integer scaling passed after resizing and maximizing.");
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
        if (!host.SetWindowSize(dimensions.first, dimensions.second) || !host.SetIntegerScaling(true) ||
            !host.Present(true))
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
        if (!host.SetIntegerScaling(false) || !host.Present(true) || !CheckFittedPixels(host, argv[1], pixels) ||
            !host.PushPointer(123, 45, true) || !Next(host, KauaiSDL::EventKind::buttonDown, event) ||
            std::abs(event.x - 123) > 0.001f || std::abs(event.y - 45) > 0.001f || !host.PushPointer(123, 45, false) ||
            !Next(host, KauaiSDL::EventKind::buttonUp, event))
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
        if (!host.SetIntegerScaling(false) || host.IntegerScaling() || !host.Present(true) ||
            !host.Presentation(view) || std::abs(view.width - 320 * scale) >= 1 ||
            std::abs(view.height - 240 * scale) >= 1)
            return Fail(__LINE__, host);
        if (!host.PushPointer(123, 45, true) || !Next(host, KauaiSDL::EventKind::buttonDown, event) ||
            std::abs(event.x - 123) > 0.001f || std::abs(event.y - 45) > 0.001f || !host.PushPointer(123, 45, false) ||
            !Next(host, KauaiSDL::EventKind::buttonUp, event))
            return Fail(__LINE__, host);
        if (!(external ? SDL_SetWindowFullscreen(window, false) : host.SetFullscreen(false)) || !SDL_SyncWindow(window))
            return Fail(__LINE__, host);
        while (host.Poll(event))
        {
        }
        if (!host.Present(true) || !CheckFittedPixels(host, argv[1], pixels))
            return Fail(__LINE__, host);
    }
    // Movie-only presentation crops the existing canvas, while input remains
    // in original canvas coordinates and each scaling policy still applies.
    const KauaiPixels::DamageRect source{40, 30, 240, 142};
    if (!host.SetWindowSize(1000, 700) || !host.SetPresentationSource(&source))
        return Fail(__LINE__, host);
    for (bool integer : {false, true})
    {
        KauaiSDL::PresentationStatus view;
        if (!host.SetIntegerScaling(integer) || !host.Present(true) || !host.Presentation(view) || view.sourceX != 40 ||
            view.sourceY != 30 || view.sourceWidth != 200 || view.sourceHeight != 112 || !host.SaveBMP(argv[1]))
            return Fail(__LINE__, host);
        float scale = std::min(view.outputWidth / 200.0f, view.outputHeight / 112.0f);
        if (integer)
            scale = std::floor(scale);
        if (std::abs(view.width - 200 * scale) >= 1 || std::abs(view.height - 112 * scale) >= 1)
            return Fail(__LINE__, host);
        auto image = SDL_LoadBMP(argv[1]);
        if (!image)
            return Fail(__LINE__, host);
        for (const auto &point : {std::pair<int, int>{0, 0}, {100, 56}, {199, 111}})
        {
            Uint8 r, g, b;
            if (!SDL_ReadSurfacePixel(image, int((point.first + 0.5f) * view.width / 200),
                                      int((point.second + 0.5f) * view.height / 112), &r, &g, &b, nullptr) ||
                ((std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b) !=
                    (pixels[(point.second + 30) * 320 + point.first + 40] & 0xFFFFFF))
                return Fail(__LINE__, host);
        }
        SDL_DestroySurface(image);
        float desktopX, desktopY, canvasX, canvasY;
        if (!host.CanvasToDesktop(123, 75, desktopX, desktopY) ||
            !host.DesktopToCanvas(desktopX, desktopY, canvasX, canvasY) || std::abs(canvasX - 123) > 0.001f ||
            std::abs(canvasY - 75) > 0.001f || !host.PushPointer(123, 75, true) ||
            !Next(host, KauaiSDL::EventKind::buttonDown, event) || std::abs(event.x - 123) > 0.001f ||
            std::abs(event.y - 75) > 0.001f || !host.PushPointer(123, 75, false) ||
            !Next(host, KauaiSDL::EventKind::buttonUp, event))
            return Fail(__LINE__, host);
    }
    // A higher-resolution image replaces the selected source without changing
    // its logical shape or pointer coordinates. Invalid replacements are atomic.
    std::vector<std::uint32_t> high(800 * 448, 0x0012AB34), copied;
    int highWidth, highHeight;
    KauaiSDL::PresentationStatus highView;
    if (!host.SetPresentationPixels(high.data(), 800, 448, 800 * 4) || !host.Present(true) ||
        !host.Presentation(highView) || highView.sourceWidth != 200 || highView.frameWidth != 800 ||
        highView.frameHeight != 448 || !host.CopyPresentationPixels(copied, highWidth, highHeight) ||
        highWidth != 800 || highHeight != 448 || copied != high ||
        host.SetPresentationPixels(high.data(), 4097, 448, 4097 * 4) || !host.SaveBMP(argv[1]))
        return Fail(__LINE__, host);
    auto highImage = SDL_LoadBMP(argv[1]);
    Uint8 red, green, blue;
    if (!highImage ||
        !SDL_ReadSurfacePixel(highImage, highImage->w / 2, highImage->h / 2, &red, &green, &blue, nullptr) ||
        red != 0x12 || green != 0xAB || blue != 0x34)
        return Fail(__LINE__, host);
    SDL_DestroySurface(highImage);
    // Palettized frames use the same capture API, support padded rows, and
    // update when either pixels or the palette changes. Repeated immutable
    // frames must survive readback and cursor-free presentation unchanged.
    Color palette[256]{};
    std::vector<std::uint8_t> indices(804 * 448, 0xEE);
    for (int y = 0; y < 448; ++y)
        std::fill_n(indices.data() + y * 804, 800, 3);
    palette[3] = {0x34, 0xAB, 0x12, 0};
    palette[4] = {0x56, 0x78, 0x9A, 0};
    for (unsigned frame = 1; frame <= 3; ++frame)
    {
        if (frame == 2)
            for (int y = 0; y < 448; ++y)
                std::fill_n(indices.data() + y * 804, 800, 4);
        if (frame == 3)
            palette[4] = palette[3];
        const auto expected = frame == 2 ? 0x009A7856u : 0x0012AB34u;
        if (!host.SetIndexedPresentationPixels(indices.data(), 800, 448, 804, palette, true, frame == 1 ? 1 : 2) ||
            !host.Present(true) || !host.Present(true) || !host.SaveBMP(argv[1], false) ||
            !host.CopyPresentationPixels(copied, highWidth, highHeight) || highWidth != 800 || highHeight != 448 ||
            !std::all_of(copied.begin(), copied.end(), [expected](auto pixel) { return pixel == expected; }))
            return Fail(__LINE__, host);
        auto image = SDL_LoadBMP(argv[1]);
        if (!image || !SDL_ReadSurfacePixel(image, image->w / 2, image->h / 2, &red, &green, &blue, nullptr) ||
            ((std::uint32_t(red) << 16) | (std::uint32_t(green) << 8) | blue) != expected)
            return Fail(__LINE__, host);
        SDL_DestroySurface(image);
    }
    // Cursor AND/XOR masks and hotspots must use logical coordinates even when
    // the presented pixels have four times the resolution of the canvas crop.
    const auto highCursor = host.CreateCursor(andBits, xorBits, 8, 2, 2, 1, 1);
    if (!highCursor || !host.SetCursor(highCursor) || !host.SetWindowSize(800, 448) ||
        !host.SetPresentationPixels(high.data(), 800, 448, 800 * 4, true))
        return Fail(__LINE__, host);
    for (bool indexed : {false, true})
    {
        if (indexed && !host.SetIndexedPresentationPixels(indices.data(), 800, 448, 804, palette, true, 2))
            return Fail(__LINE__, host);
        for (const auto position : {std::pair<int, int>{140, 86}, {40, 30}})
        {
            if (!host.PushPointer(float(position.first), float(position.second), false) ||
                !Next(host, KauaiSDL::EventKind::buttonUp, event) || !host.SaveBMP(argv[1], true))
                return Fail(__LINE__, host);
            auto image = SDL_LoadBMP(argv[1]);
            if (!image || image->w != 800 || image->h != 448)
                return Fail(__LINE__, host);
            const int left = position.first - 1 - source.left, top = position.second - 1 - source.top;
            for (int y = std::max(0, top * 4); y < (top + 2) * 4; ++y)
                for (int x = std::max(0, left * 4); x < (left + 8) * 4; ++x)
                {
                    const int cx = x / 4 - left, cy = y / 4 - top;
                    const auto bit = 0x80 >> cx;
                    auto expected = 0x12AB34u;
                    if (!(andBits[cy * 2] & bit))
                        expected = 0;
                    if (xorBits[cy * 2] & bit)
                        expected ^= 0xFFFFFF;
                    Uint8 r, g, b;
                    if (!SDL_ReadSurfacePixel(image, x, y, &r, &g, &b, nullptr) ||
                        ((std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b) != expected)
                        return Fail(__LINE__, host);
                }
            SDL_DestroySurface(image);
        }
    }
    host.DestroyCursor(highCursor);
    if (!host.CopyPresentationPixels(copied, highWidth, highHeight) || copied != high)
        return Fail(__LINE__, host);
    if (!host.SetPresentationPixels(nullptr))
        return Fail(__LINE__, host);
    for (const KauaiPixels::DamageRect invalid :
         {KauaiPixels::DamageRect{-1, 0, 20, 20}, {0, 0, 321, 240}, {1, 1, 1, 10}})
        if (host.SetPresentationSource(&invalid))
            return Fail(__LINE__, host);
    if (!host.SetPresentationSource(nullptr) || !host.SetIntegerScaling(false) || !host.Present(true) ||
        !CheckFittedPixels(host, argv[1], pixels))
        return Fail(__LINE__, host);
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
