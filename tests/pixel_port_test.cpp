/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// Real CPU/POSIX GraphicsPort, with no window creation or diagnostic UI.
#include "pixel_buffer.h"
#include "dib_frame.h"
#include "font_host.h"
#include "pixel_canvas.h"
#include "sdl_host.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "frame.h"
#undef size
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    std::fprintf(stderr, "Pixel port assertion: %s:%d %s\n", file, static_cast<int>(line), message ? message : "");
    std::fflush(stderr);
    std::_Exit(2); // Failed headless tests must not invoke crash-reporting UI.
}
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Pixel port check failed: %d\n", __LINE__);                                           \
            return false;                                                                                              \
        }                                                                                                              \
    } while (false)
static PDynamicArray Colors(int count, bool gray)
{
    auto result = DynamicArray::PglNew(sizeof(Color), count);
    if (!result || !result->FSetIvMac(count))
        return nullptr;
    for (int i = 0; i < count; ++i)
    {
        byte level = gray ? static_cast<byte>(i * 255 / (count - 1)) : 0;
        Color color{level, level, level, 0};
        result->Put(i, &color);
    }
    return result;
}
// Independent packed-byte oracle; expected transfers never call the renderer.
static std::uint32_t ReadPixel(const byte *pixels, int pitch, int depth, int x, int y)
{
    const auto row = pixels + pitch * y;
    if (depth < 8)
        return (row[x * depth / 8] >> (8 - depth - x * depth % 8)) & ((1 << depth) - 1);
    std::uint32_t value = 0;
    for (int i = 0; i < depth / 8; ++i)
        value |= std::uint32_t(row[x * (depth / 8) + i]) << (8 * i);
    return value;
}
static void WritePixel(byte *pixels, int pitch, int depth, int x, int y, std::uint32_t value)
{
    auto row = pixels + pitch * y;
    if (depth < 8)
    {
        const int shift = 8 - depth - x * depth % 8, mask = ((1 << depth) - 1) << shift;
        row[x * depth / 8] = static_cast<byte>((row[x * depth / 8] & ~mask) | ((value << shift) & mask));
    }
    else
        for (int i = 0; i < depth / 8; ++i)
            row[x * (depth / 8) + i] = static_cast<byte>(value >> (8 * i));
}
static bool Transfers()
{
    RC bounds(10, 20, 27, 29);
    GraphicsDrawingData drawing = {};
    for (auto depth : {1, 4, 8, 16, 24, 32})
    {
        auto source = GraphicsPort::PgptNewOffscreen(&bounds, depth);
        auto destination = GraphicsPort::PgptNewOffscreen(&bounds, depth);
        CHECK(source && destination);
        if (depth == 4)
        {
            auto gray = Colors(16, true);
            CHECK(gray);
            source->SetOffscreenColors(gray);
            destination->SetOffscreenColors(gray);
            ReleasePpo(&gray);
        }
        const int pitch = static_cast<int>(source->CbRow()), bytes = pitch * 9;
        auto input = source->PrgbLockPixels();
        auto output = destination->PrgbLockPixels();
        for (int i = 0; i < bytes; ++i)
            input[i] = static_cast<byte>(i * 47 + 13);
        // Rectangular clip and a region with a hole, in a nonzero-base port.
        RC global(12, 21, 25, 28), hole(16, 23, 19, 25), clip(1, 2, 14, 8);
        auto region = Region::PregnNew(&global);
        CHECK(region && region->FDiffRc(&hole));
        destination->ClipToRegn(&region);
        drawing.prcsClip = &clip;
        const auto visible = [](int x, int y) {
            return x >= 2 && x < 14 && y >= 2 && y < 8 && !(x >= 6 && x < 9 && y >= 3 && y < 5);
        };
        SystemRectangle src{1, 1, 6, 4};
        for (int scaleX : {1, 2})
        {
            SystemRectangle dst{3, 2, 3 + 5 * scaleX, 8};
            std::memset(output, 0xA5, bytes);
            std::vector<byte> expected(output, output + bytes);
            destination->CopyPixels(source, &src, &dst, &drawing);
            for (int y = 2; y < 8; ++y)
                for (int x = 3; x < dst.right; ++x)
                    if (visible(x, y))
                        WritePixel(expected.data(), pitch, depth, x, y,
                                   ReadPixel(input, pitch, depth, 1 + (x - 3) / scaleX, 1 + (y - 2) / 2));
            if (std::memcmp(output, expected.data(), bytes))
            {
                for (int i = 0; i < bytes; ++i)
                    if (output[i] != expected[i])
                    {
                        std::fprintf(stderr, "Copy depth %d scale %d byte %d: %u expected %u\n", depth, scaleX, i,
                                     output[i], expected[i]);
                        break;
                    }
                CHECK(false);
            }
        }
        // Overlapping self-copy must read the original source pixels.
        std::memcpy(output, input, bytes);
        std::vector<byte> expected(input, input + bytes);
        SystemRectangle selfSrc{1, 1, 12, 7}, selfDst{3, 2, 14, 8};
        destination->CopyPixels(destination, &selfSrc, &selfDst, &drawing);
        for (int y = 2; y < 8; ++y)
            for (int x = 3; x < 14; ++x)
                if (visible(x, y))
                    WritePixel(expected.data(), pitch, depth, x, y, ReadPixel(input, pitch, depth, x - 2, y - 1));
        CHECK(!std::memcmp(output, expected.data(), bytes));
        // Vertical scroll avoids the special legacy monochrome horizontal
        // byte-edge policy, which has its own GDI fixtures in pixel-canvas.
        SystemRectangle scroll{0, 0, 17, 9};
        for (int dy : {-2, 2})
        {
            std::memcpy(output, input, bytes);
            expected.assign(input, input + bytes);
            destination->ScrollRcs(&scroll, 0, dy, &drawing);
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 17; ++x)
                    if (visible(x, y) && visible(x, y - dy))
                        WritePixel(expected.data(), pitch, depth, x, y, ReadPixel(input, pitch, depth, x, y - dy));
            CHECK(!std::memcmp(output, expected.data(), bytes));
        }
        // Masked artwork has a nonzero origin and transparent holes. Exercise
        // same-size and scaled dispatch, ordinary sprites and solid masks.
        byte sprite[] = {255, 0, 255, 255, 0, 255, 255, 0, 255, 255, 0, 255};
        RC spriteBounds(0, 0, 4, 3);
        for (int mask : {0, 1})
        {
            auto bitmap =
                MaskedBitmapMBMP::PmbmpNew(sprite, 4, 3, &spriteBounds, 4, 3, 0, mask ? fmbmpMask : fmbmpNil, 255);
            CHECK(bitmap);
            for (int scale : {1, 2})
            {
                SystemRectangle dst{3, 2, 3 + 4 * scale, 2 + 3 * scale};
                std::memset(output, 0x55, bytes);
                expected.assign(bytes, 0x55);
                destination->DrawMbmp(bitmap, &dst, &drawing);
                const std::uint32_t white = depth <= 8 ? (1u << depth) - 1 : depth == 16 ? 0x7FFF : 0xFFFFFF;
                for (int y = dst.top; y < dst.bottom; ++y)
                    for (int x = dst.left; x < dst.right; ++x)
                        if (visible(x, y) && sprite[((y - 2) / scale) * 4 + (x - 3) / scale])
                            WritePixel(expected.data(), pitch, depth, x, y, white);
                CHECK(!std::memcmp(output, expected.data(), bytes));
            }
            ReleasePpo(&bitmap);
        }
        source->Unlock();
        destination->Unlock();
        ReleasePpo(&source);
        ReleasePpo(&destination);
    }
    return true;
}
static bool PaletteTransfers()
{
    RC bounds(0, 0, 8, 4);
    auto source = GraphicsPort::PgptNewOffscreen(&bounds, 8);
    auto indexed = GraphicsPort::PgptNewOffscreen(&bounds, 8);
    auto rgb = GraphicsPort::PgptNewOffscreen(&bounds, 32);
    auto sourceColors = Colors(256, false), destinationColors = Colors(256, false);
    CHECK(source && indexed && rgb && sourceColors && destinationColors);
    Color red{0, 0, 255, 0};
    sourceColors->Put(7, &red);
    destinationColors->Put(19, &red);
    source->SetOffscreenColors(sourceColors);
    indexed->SetOffscreenColors(destinationColors);
    ReleasePpo(&sourceColors);
    ReleasePpo(&destinationColors);
    auto input = source->PrgbLockPixels();
    input[0] = 7;
    auto indices = indexed->PrgbLockPixels();
    auto pixels = rgb->PrgbLockPixels();
    GraphicsDrawingData drawing = {};
    SystemRectangle single{0, 0, 1, 1};
    indexed->CopyPixels(source, &single, &single, &drawing);
    rgb->CopyPixels(source, &single, &single, &drawing);
    CHECK(indices[0] == 19 && pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 255 && pixels[3] == 0);
    // Preserve the legacy distinction: same-size 8-bit artwork writes raw
    // indices, while stretching it into a private palette translates colors.
    byte sprite[] = {249, 249, 249, 249}; // Application identity red.
    RC spriteBounds(0, 0, 2, 2);
    auto bitmap = MaskedBitmapMBMP::PmbmpNew(sprite, 2, 2, &spriteBounds, 0, 0, 0);
    CHECK(bitmap);
    SystemRectangle same{0, 0, 2, 2}, scaled{0, 0, 4, 4};
    indexed->DrawMbmp(bitmap, &same, &drawing);
    CHECK(indices[0] == 249 && indices[indexed->CbRow() + 1] == 249);
    indexed->DrawMbmp(bitmap, &scaled, &drawing);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(indices[y * indexed->CbRow() + x] == 19);
    ReleasePpo(&bitmap);
    // This existing conversion still requires a native quantization policy.
    // POSIX must report it without silently changing or erasing the pixels.
    LegacyLong error;
    CHECK(!vpers->FPop(&error));
    std::vector<byte> before(indices, indices + indexed->CbRow() * 4);
    indexed->CopyPixels(rgb, &single, &single, &drawing);
    CHECK(vpers->FPop(&error) && error == ercGfxCantDraw && !vpers->FPop(&error));
    CHECK(!std::memcmp(indices, before.data(), before.size()));
    source->Unlock();
    indexed->Unlock();
    rgb->Unlock();
    ReleasePpo(&source);
    ReleasePpo(&indexed);
    ReleasePpo(&rgb);
    return true;
}
#ifdef KAUAI_TEST_FREETYPE
static bool TextDrawing()
{
#ifdef _WIN32
    _putenv_s("KAUAI_FONT_PROVIDER", "freetype");
#endif
    CHECK(vntl.FInit());
    achar familyName[] = PszLit("Arial");
    String family(familyName);
    long number;
    CHECK(vntl.FGetOnn(&family, &number));
    CHECK(!vntl.FFixedPitch(number));
    CHECK(vntl.OnnSystem() >= 0 && vntl.OnnSystem() < vntl.OnnMac());
    RC bounds(10, 20, 127, 87), clip(5, 4, 111, 61), global(17, 26, 119, 79), hole(59, 43, 68, 59);
    constexpr int width = 117, height = 67;
    achar text[] = PszLit("AV fj\xE9");
    const int length = sizeof(text) / sizeof(achar) - 1;
    const KauaiFonts::Text input{text, length,
                                 sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes};
    int cases = 0;
    for (int depth : {1, 4, 8, 16, 24, 32})
    {
        auto port = GraphicsPort::PgptNewOffscreen(&bounds, depth);
        CHECK(port);
        if (depth == 4)
        {
            auto gray = Colors(16, true);
            CHECK(gray);
            port->SetOffscreenColors(gray);
            ReleasePpo(&gray);
        }
        auto region = Region::PregnNew(&global);
        CHECK(region && region->FDiffRc(&hole));
        port->ClipToRegn(&region);
        const int pitch = static_cast<int>(port->CbRow()), bytes = pitch * height;
        auto pixels = port->PrgbLockPixels();
        for (int fontHeight : {9, 16})
            for (int style : {0, fontBold | fontItalic, int(fontUnderline)})
            {
                auto face = KauaiFonts::CreateStudioFace({"Arial", 5}, fontHeight, (style & fontBold) != 0,
                                                         (style & fontItalic) != 0, (style & fontUnderline) != 0);
                CHECK(face);
                KauaiFonts::Metrics metrics;
                CHECK(face->Measure(input, metrics));
                for (int horizontal = 0; horizontal < tahLim; ++horizontal)
                    for (int vertical = 0; vertical < tavLim; ++vertical)
                        for (int background : {0, 1, 2})
                        {
                            FontDescription font{static_cast<LegacyLong>(number), static_cast<LegacyUlong>(style),
                                                 fontHeight, horizontal, vertical};
                            SystemPoint point{58, 33};
                            SystemRectangle measured;
                            port->GetRcsFromRgch(&measured, text, length, point, &font);
                            const int h = metrics.cellHeight + metrics.externalLeading;
                            const int left = point.x - (horizontal == tahLeft     ? 0
                                                        : horizontal == tahCenter ? metrics.advance / 2
                                                                                  : metrics.advance);
                            const int top = point.y - (vertical == tavTop        ? 0
                                                       : vertical == tavCenter   ? h / 2
                                                       : vertical == tavBaseline ? metrics.ascent
                                                                                 : h);
                            CHECK(measured.left == left && measured.right == left + metrics.advance &&
                                  measured.top == top && measured.bottom == top + h);
                            const int cellLeft = horizontal == tahCenter ? point.x - (metrics.advance + 1) / 2 : left;
                            const int cellTop = vertical == tavBottom ? point.y - metrics.cellHeight : top;
                            KauaiFonts::Placement placement{
                                {left, top, left + metrics.advance, top + h},
                                {cellLeft, cellTop, cellLeft + metrics.advance, cellTop + metrics.cellHeight}};
                            GraphicsDrawingData drawing = {};
                            drawing.acrFore = kacrWhite;
                            drawing.acrBack = background == 0 ? kacrClear : background == 1 ? kacrBlack : kacrInvert;
                            drawing.prcsClip = &clip;
                            std::memset(pixels, 0x55, bytes);
                            std::vector<byte> expected(pixels, pixels + bytes);
                            KauaiFonts::Mask mask;
                            KauaiFonts::Image32 image;
                            std::vector<std::uint32_t> reference(width * height);
                            if (depth >= 16)
                            {
                                // Reference color rendering is independent of port dispatch,
                                // clipping and packed-format conversion.
                                for (auto &pixel : reference)
                                    pixel = depth == 16 ? 0xAD52AD : depth == 24 ? 0x555555 : 0x55555555;
                                CHECK(face->Paint32(input, {reference.data(), width, height, width * 4}, placement,
                                                    0xFFFFFF,
                                                    background == 0   ? KauaiFonts::Background::transparent
                                                    : background == 1 ? KauaiFonts::Background::opaque
                                                                      : KauaiFonts::Background::invert,
                                                    0, image));
                            }
                            else
                                CHECK(face->Rasterize(input, mask, background == 1));
                            for (int y = 6; y < 59; ++y)
                                for (int x = 7; x < 109; ++x)
                                {
                                    if (x >= 49 && x < 58 && y >= 23 && y < 39)
                                        continue;
                                    auto raw = ReadPixel(expected.data(), pitch, depth, x, y);
                                    if (depth >= 16)
                                    {
                                        if (x < image.left || x >= image.left + image.width || y < image.top ||
                                            y >= image.top + image.height)
                                            continue;
                                        raw = image.pixels[(y - image.top) * image.width + x - image.left];
                                        if (depth == 16)
                                            raw = ((raw >> 9) & 0x7C00) | ((raw >> 6) & 0x3E0) | ((raw >> 3) & 0x1F);
                                    }
                                    else
                                    {
                                        const int mx = x - cellLeft - mask.left, my = y - cellTop - mask.top;
                                        const bool covered = mx >= 0 && mx < mask.width && my >= 0 && my < mask.height;
                                        if (background == 2 && x >= left && x < left + metrics.advance && y >= top &&
                                            y < top + h)
                                            raw ^= (1u << depth) - 1;
                                        if (background == 1 &&
                                            (mask.background.empty()
                                                 ? x >= cellLeft && x < cellLeft + metrics.advance && y >= cellTop &&
                                                       y < cellTop + metrics.cellHeight
                                                 : covered && mask.background[my * mask.pitch + mx] >= 128))
                                            raw = 0;
                                        if (covered && mask.coverage[my * mask.pitch + mx] >= 128)
                                            raw = (1u << depth) - 1;
                                    }
                                    WritePixel(expected.data(), pitch, depth, x, y, raw);
                                }
                            const auto before = KauaiFonts::FontProviderStats();
                            port->DrawRgch(text, length, point, &drawing, &font);
                            const auto after = KauaiFonts::FontProviderStats();
                            CHECK(after.portable == before.portable); // Measurement cached the selected face.
                            if (std::memcmp(pixels, expected.data(), bytes))
                            {
                                for (int y = 0; y < height; ++y)
                                    for (int x = 0; x < width; ++x)
                                        if (ReadPixel(pixels, pitch, depth, x, y) !=
                                            ReadPixel(expected.data(), pitch, depth, x, y))
                                        {
                                            std::fprintf(
                                                stderr,
                                                "At %d,%d actual=%x expected=%x left/top=%d,%d mask=%d,%d %dx%d\n", x,
                                                y, ReadPixel(pixels, pitch, depth, x, y),
                                                ReadPixel(expected.data(), pitch, depth, x, y), left, cellTop,
                                                mask.left, mask.top, mask.width, mask.height);
                                            goto mismatch;
                                        }
                            mismatch:
                                std::fprintf(
                                    stderr, "Text mismatch depth=%d style=%d horizontal=%d vertical=%d background=%d\n",
                                    depth, style, horizontal, vertical, background);
                                CHECK(false);
                            }
                            CHECK(vpers->Cerc() == 0);
                            ++cases;
                        }
            }
        port->Unlock();
        ReleasePpo(&port);
    }
    // Boxes share the original gray frame and preserve clipping. Monochrome
    // patterned frames still need a portable implementation (native fallback
    // on Windows), so verify the supported formats here.
    for (int depth : {4, 8, 16, 24, 32})
    {
        auto port = GraphicsPort::PgptNewOffscreen(&bounds, depth);
        CHECK(port);
        if (depth == 4)
        {
            auto gray = Colors(16, true);
            CHECK(gray);
            port->SetOffscreenColors(gray);
            ReleasePpo(&gray);
        }
        auto pixels = port->PrgbLockPixels();
        const int pitch = static_cast<int>(port->CbRow()), bytes = pitch * height;
        GraphicsDrawingData drawing = {};
        drawing.acrFore = kacrWhite;
        drawing.acrBack = kacrClear;
        drawing.prcsClip = &clip;
        FontDescription font{static_cast<LegacyLong>(number), 0, 16, tahLeft, tavTop};
        SystemPoint point{4, 10};
        SystemRectangle measured;
        port->GetRcsFromRgch(&measured, text, length, point, &font);
        std::memset(pixels, 0x55, bytes);
        port->DrawRgch(text, length, point, &drawing, &font);
        std::vector<byte> expected(pixels, pixels + bytes);
        for (int y = measured.top; y < measured.bottom; ++y)
            for (int x = measured.left; x < measured.right; ++x)
                if (x >= clip.xpLeft && x < clip.xpRight && y >= clip.ypTop && y < clip.ypBottom &&
                    (x == measured.left || x == measured.right - 1 || y == measured.top || y == measured.bottom - 1) &&
                    (x + y) % 2 == 0)
                    WritePixel(expected.data(), pitch, depth, x, y,
                               depth <= 8    ? (1u << depth) - 1
                               : depth == 16 ? 0x7FFF
                                             : 0xFFFFFF);
        std::memset(pixels, 0x55, bytes);
        font.grfont = fontBoxed;
        port->DrawRgch(text, length, point, &drawing, &font);
        CHECK(!std::memcmp(pixels, expected.data(), bytes) && !vpers->Cerc());
        font.grfont = 0;
        expected.assign(pixels, pixels + bytes);
        port->DrawRgch(text, 0, point, &drawing, &font);
        CHECK(!std::memcmp(pixels, expected.data(), bytes));
        port->GetRcsFromRgch(&measured, text, 0, point, &font);
        CHECK(measured.left == measured.right && !vpers->Cerc());
        port->GetRcsFromRgch(&measured, text, -1, point, &font);
        LegacyLong error;
        CHECK(vpers->FPop(&error) && error == ercGfxCantSetFont && !vpers->Cerc());
        CHECK(measured.left == 0 && measured.top == 0 && measured.right == 0 && measured.bottom == 0);
        port->DrawRgch(text, -1, point, &drawing, &font);
        CHECK(vpers->FPop(&error) && error == ercGfxCantDraw && !vpers->Cerc());
        CHECK(!std::memcmp(pixels, expected.data(), bytes));
        port->Unlock();
        ReleasePpo(&port);
    }
    const auto stats = KauaiFonts::FontProviderStats();
    CHECK(stats.portable && stats.portableEnumerations && !stats.native && !stats.nativeBootstraps &&
          !stats.nativeDraws && !stats.nativeMeasurements && !stats.nativeEnumerations);
    std::printf("Portable GraphicsPort text: %d exact pixel/alignment cases without native fonts passed.\n", cases);
    return true;
}
#endif
static bool Run()
{
    const auto baseline = KauaiPixels::PixelBuffers();
    RC bounds(10, 20, 43, 23), empty;
    CHECK(!GraphicsPort::PgptNewOffscreen(nullptr, 32));
    CHECK(!GraphicsPort::PgptNewOffscreen(&empty, 32));
    CHECK(!GraphicsPort::PgptNewOffscreen(&bounds, 3));
    CHECK(!GraphicsPort::PgptNewOffscreen(&bounds, 4)); // No host palette bootstrap.
    auto colors = Colors(256, false);
    CHECK(colors);
    GraphicsPort::SetActiveColors(colors, fpalIdentity);
    CHECK(Transfers());
    CHECK(PaletteTransfers());
#ifdef KAUAI_TEST_FREETYPE
    CHECK(TextDrawing());
#endif
    auto active = GraphicsPort::PglclrGetPalette();
    CHECK(active && active->IvMac() == 256);
    Color red;
    active->Get(249, &red);
    CHECK(red.bRed == 255 && red.bGreen == 0 && red.bBlue == 0);
    ReleasePpo(&active);
    const long realization = vcactRealize;
    GraphicsPort::SetActiveColors(nullptr, fpalAnimate);
    CHECK(vcactRealize == realization);
    GraphicsDrawingData drawing = {};
    drawing.acrFore = kacrWhite;
    SystemRectangle single = {1, 1, 2, 2};
    for (auto depth : {1, 4, 8, 16, 24, 32})
    {
        auto port = GraphicsPort::PgptNewOffscreen(&bounds, depth);
        CHECK(port && port->CbitPixel() == depth);
        const int pitch = ((33 * depth + 31) / 32) * 4;
        CHECK(port->CbRow() == pitch);
        PT origin;
        port->GetPtBase(&origin);
        CHECK(origin.xp == 10 && origin.yp == 20);
        if (depth == 4)
        {
            auto own = Colors(16, true);
            CHECK(own);
            port->SetOffscreenColors(own);
            ReleasePpo(&own);
        }
        port->DrawRcs(&single, &drawing);
        RC local;
        const auto pixels = port->PrgbLockPixels(&local);
        CHECK(pixels && local.xpLeft == 0 && local.ypTop == 0 && local.Dxp() == 33 && local.Dyp() == 3);
        std::vector<byte> expected(pitch * 3, 0);
        switch (depth)
        {
        case 1:
            expected[pitch] = 0x40;
            break;
        case 4:
            expected[pitch] = 0x0F;
            break;
        case 8:
            expected[pitch + 1] = 255;
            break;
        case 16:
            expected[pitch + 2] = 255;
            expected[pitch + 3] = 127;
            break;
        case 24:
            expected[pitch + 3] = expected[pitch + 4] = expected[pitch + 5] = 255;
            break;
        case 32:
            expected[pitch + 4] = expected[pitch + 5] = expected[pitch + 6] = 255;
            break;
        }
        CHECK(!std::memcmp(pixels, expected.data(), expected.size()));
        port->Unlock();
        ReleasePpo(&port);
        CHECK(KauaiPixels::PixelBuffers().liveBuffers == baseline.liveBuffers);
    }
    // Changing the application palette must not overwrite an explicit port
    // palette. Returning to the application palette takes effect on the next draw.
    auto indexed = GraphicsPort::PgptNewOffscreen(&bounds, 8);
    CHECK(indexed);
    auto own = Colors(256, false);
    CHECK(own);
    Color privateRed{0, 0, 255, 0};
    own->Put(7, &privateRed);
    indexed->SetOffscreenColors(own);
    ReleasePpo(&own);
    GraphicsPort::SetActiveColors(colors, fpalIdentity);
    drawing.acrFore = kacrRed;
    indexed->DrawRcs(&single, &drawing);
    auto pixels = indexed->PrgbLockPixels();
    CHECK(pixels[indexed->CbRow() + 1] == 7);
    indexed->Unlock();
    indexed->SetOffscreenColors(nullptr);
    indexed->DrawRcs(&single, &drawing);
    pixels = indexed->PrgbLockPixels();
    CHECK(pixels[indexed->CbRow() + 1] == 249);
    indexed->Unlock();
    ReleasePpo(&indexed);

    auto port = GraphicsPort::PgptNewOffscreen(&bounds, 32);
    CHECK(port);
    RC globalClip(11, 21, 13, 23), recovered;
    auto region = Region::PregnNew(&globalClip);
    CHECK(region);
    port->ClipToRegn(&region);
    CHECK(!region);
    SystemRectangle all{0, 0, 33, 3};
    drawing.acrFore = kacrWhite;
    port->DrawRcs(&all, &drawing);
    pixels = port->PrgbLockPixels();
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 33; ++x)
        {
            const auto p = pixels + y * port->CbRow() + 4 * x;
            const byte expected = y >= 1 && x >= 1 && x < 3 ? 255 : 0;
            CHECK(p[0] == expected && p[1] == expected && p[2] == expected && p[3] == 0);
        }
    port->Unlock();
    port->ClipToRegn(&region);
    CHECK(region && region->FIsRc(&recovered) && recovered == globalClip);
    ReleasePpo(&region);
    // Video uses the same port storage, not a display API.
    KauaiPixels::DibFrame frame;
    frame.width = frame.height = 1;
    frame.pitch = 4;
    frame.pixels = {3, 5, 7, 0};
    CHECK(port->DrawVideoFrame(frame, &single, &drawing));
    pixels = port->PrgbLockPixels();
    CHECK(!std::memcmp(pixels + port->CbRow() + 4, frame.pixels.data(), 4));
    // Exercise the public POSIX dispatchers as well as their shared draw code.
    std::memset(pixels, 0, port->CbRow() * 3);
    drawing.dxpPen = drawing.dypPen = 1;
    SystemPoint first{1, 1}, last{4, 1};
    port->DrawLine(&first, &last, &drawing);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 33; ++x)
        {
            const auto p = pixels + y * port->CbRow() + 4 * x;
            const byte expected = y == 1 && x >= 1 && x <= 4 ? 255 : 0;
            CHECK(p[0] == expected && p[1] == expected && p[2] == expected);
        }
    std::vector<byte> before(pixels, pixels + port->CbRow() * 3);
    port->HiliteRcs(&all, &drawing);
    CHECK(std::memcmp(before.data(), pixels, before.size()));
    port->HiliteRcs(&all, &drawing);
    CHECK(!std::memcmp(before.data(), pixels, before.size()));
    std::memset(pixels, 0, port->CbRow() * 3);
    SystemRectangle oval{1, 0, 5, 3};
    port->DrawOval(&oval, &drawing);
    CHECK(pixels[port->CbRow() + 4 * 2] == 255 && pixels[port->CbRow() + 4 * 32] == 0);
    std::memset(pixels, 0, port->CbRow() * 3);
    HQ polygon = hqNil;
    CHECK(FAllocHq(&polygon, kcbOlyBase + 3 * sizeof(SystemPoint), fmemNil, mprNormal));
    auto shape = static_cast<OLY *>(PvLockHq(polygon));
    shape->cpts = 3;
    shape->rgpts[0] = {1, 0};
    shape->rgpts[1] = {5, 0};
    shape->rgpts[2] = {1, 3};
    UnlockHq(polygon);
    port->DrawPoly(polygon, &drawing);
    FreePhq(&polygon);
    CHECK(pixels[port->CbRow() + 4] == 255 && pixels[port->CbRow() + 4 * 32] == 0);
    port->Unlock();
    ReleasePpo(&port);
    ReleasePpo(&colors);
    CHECK(KauaiPixels::PixelBuffers().liveBuffers == baseline.liveBuffers &&
          KauaiPixels::PixelBuffers().liveBytes == baseline.liveBytes);
    return true;
}
static bool WindowAttachment()
{
    // This opt-in integration test must never open a desktop window.
    const auto driver = std::getenv("SDL_VIDEODRIVER");
    CHECK(driver && !std::strcmp(driver, "dummy"));
    auto &host = KauaiSDL::Main();
    const auto baseline = KauaiPixels::PixelBuffers();
    auto foreign = reinterpret_cast<CoreNative::WindowHandle>(&host);
    CHECK(!GraphicsPort::PgptNewHwnd(nullptr) && !GraphicsPort::PgptNewHwnd(foreign));
    CHECK(host.Create("Portable Studio drawing surface", 640, 480, true));
    const auto token = static_cast<CoreNative::WindowHandle>(host.WindowToken());
    CHECK(token && !host.NativeWindow());
    auto port = GraphicsPort::PgptNewHwnd(token);
    CHECK(port && port->CbitPixel() == 32 && port->CbRow() == 640 * 4);
    RC bounds, expectedBounds(0, 0, 640, 480);
    CHECK(port->PrgbLockPixels(&bounds) && bounds == expectedBounds);
    port->Unlock();
    GraphicsDrawingData drawing = {};
    drawing.acrFore = kacrRed;
    SystemRectangle rectangle{3, 5, 17, 23};
    port->DrawRcs(&rectangle, &drawing);
    CHECK(host.Present(true));
    std::vector<std::uint32_t> pixels;
    int width, height;
    CHECK(host.CopyPixels(pixels, width, height) && width == 640 && height == 480);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            CHECK(pixels[y * width + x] == (x >= 3 && x < 17 && y >= 5 && y < 23 ? 0xFF0000u : 0u));
    CHECK(host.SetWindowSize(1300, 1000));
    KauaiSDL::Event event;
    while (host.Poll(event))
    {
    }
    KauaiSDL::PresentationStatus presentation;
    CHECK(host.Present(true) && host.Presentation(presentation));
    CHECK(host.Width() == 640 && host.Height() == 480);
    const auto scale = presentation.outputWidth / 640 < presentation.outputHeight / 480
                           ? presentation.outputWidth / 640
                           : presentation.outputHeight / 480;
    CHECK(scale >= 1 && presentation.width == 640 * scale && presentation.height == 480 * scale);
    // Replacing the bound port must not let the old owner's destruction
    // unbind the new surface; releasing the current owner must unbind it.
    auto replacement = GraphicsPort::PgptNewHwnd(token);
    CHECK(replacement);
    ReleasePpo(&port);
    drawing.acrFore = kacrWhite;
    replacement->DrawRcs(&rectangle, &drawing);
    CHECK(host.CopyPixels(pixels, width, height) && pixels[5 * width + 3] == 0xFFFFFF);
    ReleasePpo(&replacement);
    CHECK(!host.CopyPixels(pixels, width, height) && !host.Present(true));
    // The host can also close before the port; reopening establishes a new
    // binding which the old port must not disturb.
    port = GraphicsPort::PgptNewHwnd(token);
    CHECK(port);
    host.Close();
    CHECK(!GraphicsPort::PgptNewHwnd(token));
    CHECK(host.Create("Reopened Studio surface", 320, 240, true));
    replacement = GraphicsPort::PgptNewHwnd(static_cast<CoreNative::WindowHandle>(host.WindowToken()));
    CHECK(replacement);
    ReleasePpo(&port);
    replacement->DrawRcs(&rectangle, &drawing);
    CHECK(host.CopyPixels(pixels, width, height) && width == 320 && height == 240 && pixels[5 * width + 3] == 0xFFFFFF);
    CHECK(host.Present(true));
    host.Close();
    ReleasePpo(&replacement);
    CHECK(KauaiPixels::PixelBuffers().liveBuffers == baseline.liveBuffers &&
          KauaiPixels::PixelBuffers().liveBytes == baseline.liveBytes);
    CHECK(!vpers->Cerc());
    std::puts("SDL root port: binding, exact pixels, integer presentation, replacement and shutdown passed.");
    return true;
}
int main(int argc, char **argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--sdl-window"))
        return WindowAttachment() ? 0 : 1;
    if (argc != 1)
        return 2;
    if (!Run())
        return 1;
    std::puts("CPU GraphicsPort: six formats, palette conversion, clipped copies/scrolls/artwork, shapes, video and "
              "release passed");
    return 0;
}
