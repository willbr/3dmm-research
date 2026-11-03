/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_services.h"
#include "font_gdi.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
static int PackedText(int depth)
{
    constexpr int width = 157, height = 91;
    const int pitch = ((width * depth + 31) / 32) * 4;
    int cases = 0;
    for (int palettePass = 0; palettePass < (depth == 4 ? 3 : 1); ++palettePass)
    {
        struct
        {
            BITMAPINFOHEADER header;
            Color colors[16];
        } info = {};
        info.header.biSize = sizeof(BITMAPINFOHEADER);
        info.header.biWidth = width;
        info.header.biHeight = -height;
        info.header.biPlanes = 1;
        info.header.biBitCount = static_cast<WORD>(depth);
        info.header.biClrUsed = depth <= 4 ? 1 << depth : 0;
        auto palette = DynamicArray::PglNew(sizeof(Color), 16);
        if (!palette || !palette->FSetIvMac(16))
            return 1;
        for (int i = 0; i < 16; ++i)
        {
            const auto gray = static_cast<byte>((palettePass ? 15 - i : i) * 17);
            info.colors[i] = {gray, gray, gray, 0};
            if (palettePass == 2)
                info.colors[i] = {static_cast<byte>(i * 73 + 11), static_cast<byte>(i * 17 + 23),
                                  static_cast<byte>(i * 37 + 7), 0};
            palette->Put(i, &info.colors[i]);
        }
        if (depth == 1)
        {
            info.colors[0] = {};
            info.colors[1] = {255, 255, 255, 0};
        }
        HDC dc = CreateCompatibleDC(nullptr);
        void *pixels = nullptr;
        auto bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!dc || !bitmap || !pixels)
            return 1;
        auto previous = SelectObject(dc, bitmap);
        auto reference = GraphicsPort::PgptNew(dc);
        RC bounds(0, 0, width, height), clip(5, 3, width - 7, height - 5), hole(70, 34, 78, 58);
        auto actual = GraphicsPort::PgptNewOffscreen(&bounds, depth);
        auto region = Region::PregnNew(&clip);
        if (!reference || !actual || !region || !region->FDiffRc(&hole))
            return 1;
        if (depth == 4)
            actual->SetOffscreenColors(palette);
        region->AddRef();
        auto owned = region;
        reference->ClipToRegn(&owned);
        region->AddRef();
        owned = region;
        actual->ClipToRegn(&owned);
        auto output = actual->PrgbLockPixels();
        if (!output || actual->CbRow() != pitch)
            return 1;
        bool nativeMonoFrame = false;
        for (auto name : {PszLit("Arial"), PszLit("Courier New"), PszLit("System")})
        {
            String family;
            family.SetSz(const_cast<achar *>(name));
            long number;
            if (!vntl.FGetOnn(&family, &number))
                return 1;
            for (long style : {0L, 1L, 2L, 4L, 8L})
                for (long horizontal = 0; horizontal < tahLim; ++horizontal)
                    for (long vertical = 0; vertical < tavLim; ++vertical)
                        for (int background = 0; background < 3; ++background)
                            for (int ink = 0; ink < 2; ++ink)
                            {
                                for (int i = 0; i < pitch * height; ++i)
                                    static_cast<byte *>(pixels)[i] = output[i] = static_cast<byte>(i * 37 + 19);
                                const achar text[] = PszLit("AV fj\xE9");
                                const long length = sizeof(text) / sizeof(achar) - 1;
                                FontDescription font = {number, static_cast<ulong>(style), 16, horizontal, vertical};
                                GraphicsDrawingData drawing = {};
                                drawing.acrFore = ink ? AbstractColor(210, 42, 8) : AbstractColor(byte(0));
                                drawing.acrBack = background == 0   ? kacrClear
                                                  : background == 1 ? AbstractColor(byte(200))
                                                                    : kacrInvert;
                                drawing.prcsClip = &clip;
                                SystemPoint point = {80, 45};
                                reference->DrawRgch(const_cast<achar *>(text), length, point, &drawing, &font);
                                actual->DrawRgch(const_cast<achar *>(text), length, point, &drawing, &font);
                                if (depth == 1)
                                {
                                    // Opaque text changes the conversion state used by
                                    // subsequent noncanonical solid monochrome colors.
                                    GraphicsDrawingData fill = {};
                                    fill.grfgdd = fgddFill;
                                    // Reuse the same brush across opaque-text background changes.
                                    fill.acrFore = AbstractColor(13, 98, 201);
                                    SystemRectangle swatch = {7, 7, 15, 12};
                                    reference->DrawRcs(&swatch, &fill);
                                    actual->DrawRcs(&swatch, &fill);
                                    nativeMonoFrame |= (style & fontBoxed) != 0;
                                }
                                GraphicsPort::Flush();
                                if (std::memcmp(pixels, output, pitch * height))
                                {
                                    for (int i = 0; i < pitch * height; ++i)
                                        if (static_cast<byte *>(pixels)[i] != output[i])
                                        {
                                            std::fprintf(stderr,
                                                         "Packed font mismatch: case=%d palette=%d style=%ld h=%ld "
                                                         "v=%ld bg=%d ink=%d byte=%d expected=%02x actual=%02x\n",
                                                         cases, palettePass, style, horizontal, vertical, background,
                                                         ink, i, static_cast<byte *>(pixels)[i], output[i]);
                                            break;
                                        }
                                    return 1;
                                }
#ifdef KAUAI_SDL3
                                if (actual->FHasNativeMirror() != (depth == 16 || (depth == 1 && nativeMonoFrame)))
                                    return 1;
#endif
                                ++cases;
                            }
        }
        actual->Unlock();
        ReleasePpo(&actual);
        ReleasePpo(&reference);
        ReleasePpo(&region);
        ReleasePpo(&palette);
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
    std::printf("Packed font host: %d native drawing comparisons at depth %d passed.\n", cases, depth);
    return 0;
}
int main()
{
    _putenv_s("KAUAI_FONT_PROVIDER", "gdi");
    if (std::strcmp(KauaiFonts::StudioFontProvider(), "gdi"))
        return 1;
    const int width = 160, height = 96;
    vwig.hdcApp = GetDC(nullptr);
    if (!vwig.hdcApp || !vntl.FInit())
        return 1;
    auto palette = DynamicArray::PglNew(sizeof(Color), 256);
    if (!palette || !palette->FSetIvMac(256))
        return 1;
    for (int i = 0; i < 256; ++i)
    {
        Color color = {static_cast<byte>(i), static_cast<byte>(i), static_cast<byte>(i), 0};
        palette->Put(i, &color);
    }
    GraphicsPort::SetActiveColors(palette, fpalIdentity);
    auto active = GraphicsPort::PglclrGetPalette();
    if (!active)
        return 1;
    Color colors[256];
    std::memcpy(colors, active->QvGet(0), sizeof(colors));
    struct
    {
        BITMAPINFOHEADER header;
        RGBQUAD palette[256];
    } info = {};
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = width;
    info.header.biHeight = -height;
    info.header.biPlanes = 1;
    info.header.biBitCount = 8;
    info.header.biClrUsed = 256;
    std::memcpy(info.palette, colors, sizeof(colors));
    HDC dc = CreateCompatibleDC(nullptr);
    void *referencePixels = nullptr;
    auto bitmap =
        CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &referencePixels, nullptr, 0);
    if (!dc || !bitmap || !referencePixels)
        return 1;
    auto previous = SelectObject(dc, bitmap);
    auto reference = GraphicsPort::PgptNew(dc);
    RC bounds(0, 0, width, height), clip(7, 5, width - 11, height - 7), hole(68, 35, 75, 58);
    auto actual = GraphicsPort::PgptNewOffscreen(&bounds, 8);
    auto region = Region::PregnNew(&clip);
    if (!reference || !actual || !region || !region->FDiffRc(&hole))
        return 1;
    region->AddRef();
    auto owned = region;
    reference->ClipToRegn(&owned);
    region->AddRef();
    owned = region;
    actual->ClipToRegn(&owned);
    byte *actualPixels = actual->PrgbLockPixels();
    if (!actualPixels)
        return 1;
    const auto pitch = actual->CbRow();
    BITMAPINFO rgbInfo = {};
    rgbInfo.bmiHeader = info.header;
    rgbInfo.bmiHeader.biBitCount = 32;
    rgbInfo.bmiHeader.biClrUsed = 0;
    HDC rgbDc = CreateCompatibleDC(nullptr);
    void *referenceRgbPixels = nullptr;
    auto rgbBitmap = CreateDIBSection(rgbDc, &rgbInfo, DIB_RGB_COLORS, &referenceRgbPixels, nullptr, 0);
    if (!rgbDc || !rgbBitmap || !referenceRgbPixels)
        return 1;
    auto previousRgb = SelectObject(rgbDc, rgbBitmap);
    auto rgbReference = GraphicsPort::PgptNew(rgbDc);
    auto rgbActual = GraphicsPort::PgptNewOffscreen(&bounds, 32);
    if (!rgbReference || !rgbActual)
        return 1;
    region->AddRef();
    owned = region;
    rgbReference->ClipToRegn(&owned);
    region->AddRef();
    owned = region;
    rgbActual->ClipToRegn(&owned);
    auto rgbPixels = rgbActual->PrgbLockPixels();
    if (!rgbPixels)
        return 1;
    const auto rgbPitch = rgbActual->CbRow();
    const achar text[] = PszLit("AV fj\xE9");
    const long length = sizeof(text) / sizeof(achar) - 1;
#ifdef KAUAI_SDL3
    // Native font measurement needs a context, but no framebuffer bitmap.
    if (rgbActual->FHasNativeContext() || rgbActual->FHasNativeMirror())
        return 1;
    FontDescription probe = {0, 0, 16, tahLeft, tavTop};
    SystemPoint probePoint = {0, 0};
    SystemRectangle expectedProbe, actualProbe;
    rgbReference->GetRcsFromRgch(&expectedProbe, const_cast<achar *>(text), length, probePoint, &probe);
    rgbActual->GetRcsFromRgch(&actualProbe, const_cast<achar *>(text), length, probePoint, &probe);
    if (!rgbActual->FHasNativeContext() || rgbActual->FHasNativeMirror() ||
        std::memcmp(&expectedProbe, &actualProbe, sizeof(expectedProbe)))
        return 1;
#endif
    int cases = 0;
    const achar *families[] = {PszLit("Arial"), PszLit("Courier New"), PszLit("System")};
    for (int palettePass = 0; palettePass < 2; ++palettePass)
    {
        if (palettePass)
        {
            auto privatePalette = DynamicArray::PglNew(sizeof(Color), 256);
            if (!privatePalette || !privatePalette->FSetIvMac(256))
                return 1;
            for (int i = 0; i < 256; ++i)
            {
                colors[i] = {static_cast<byte>(i * 17), static_cast<byte>(i * 37), static_cast<byte>(i * 73), 0};
                privatePalette->Put(i, &colors[i]);
            }
            SetDIBColorTable(dc, 0, 256, reinterpret_cast<RGBQUAD *>(colors));
            actual->SetOffscreenColors(privatePalette);
            ReleasePpo(&privatePalette);
        }
        for (auto name : families)
        {
            String family;
            family.SetSz(const_cast<achar *>(name));
            long number;
            if (!vntl.FGetOnn(&family, &number))
                return 1;
            for (long style : {0L, 1L, 2L, 4L, 8L})
                for (long horizontal = 0; horizontal < tahLim; ++horizontal)
                    for (long vertical = 0; vertical < tavLim; ++vertical)
                        for (int background = 0; background < 3; ++background)
                            for (int ink = 0; ink < 2; ++ink)
                            {
                                std::memset(referencePixels, 100, width * height);
                                std::memset(actualPixels, 100, pitch * height);
                                FontDescription font = {number, static_cast<ulong>(style), 16, horizontal, vertical};
                                GraphicsDrawingData drawing = {};
                                RC graphicsClip;
                                drawing.acrFore = ink ? AbstractColor(210, 42, 8) : AbstractColor(byte(0));
                                drawing.acrBack = background == 0   ? kacrClear
                                                  : background == 1 ? AbstractColor(byte(200))
                                                                    : kacrInvert;
                                SystemRectangle nativeClip = clip;
                                drawing.prcsClip = &(graphicsClip = RC(nativeClip));
                                SystemPoint point = {82, 48};
                                SystemRectangle expected, observed;
                                reference->GetRcsFromRgch(&expected, const_cast<achar *>(text), length, point, &font);
                                actual->GetRcsFromRgch(&observed, const_cast<achar *>(text), length, point, &font);
                                if (std::memcmp(&expected, &observed, sizeof(expected)))
                                {
                                    std::fprintf(stderr, "Font bounds mismatch: %d\n", cases);
                                    return 1;
                                }
                                if (!horizontal && !vertical && !background && !ink)
                                {
                                    auto native = vntl.HfntCreate(&font);
                                    if (!native)
                                        return 1;
                                    auto face = KauaiFonts::CreateGdiFace(native);
                                    DeleteObject(native); // The host face must own its own font lifetime.
                                    KauaiFonts::Metrics measured;
                                    KauaiFonts::Mask mask;
                                    if (!face || !face->Measure({text, static_cast<std::size_t>(length)}, measured) ||
                                        measured.advance != expected.right - expected.left ||
                                        !face->Rasterize({text, static_cast<std::size_t>(length)}, mask, true) ||
                                        mask.coverage.empty() || mask.background.empty())
                                        return 1;
                                    KauaiFonts::Metrics narrow, wide;
                                    const char16_t unicode[] = u"AV fj";
                                    if (!face->Measure({"AV fj", 5}, narrow) ||
                                        !face->Measure({unicode, 5, KauaiFonts::Encoding::utf16}, wide) ||
                                        narrow.advance != wide.advance || narrow.cellHeight != wide.cellHeight ||
                                        face->Measure({nullptr, 1}, wide))
                                        return 1;
                                }
                                reference->DrawRgch(const_cast<achar *>(text), length, point, &drawing, &font);
                                actual->DrawRgch(const_cast<achar *>(text), length, point, &drawing, &font);
                                GraphicsPort::Flush();
                                for (int y = 0; y < height; ++y)
                                    for (int x = 0; x < width; ++x)
                                        if (std::memcmp(&colors[static_cast<byte *>(referencePixels)[y * width + x]],
                                                        &colors[actualPixels[y * pitch + x]], 3))
                                        {
                                            TEXTMETRICA metric = {};
                                            GetTextMetricsA(dc, &metric);
                                            std::fprintf(
                                                stderr,
                                                "Font draw mismatch: case=%d family=%ls style=%ld h=%ld v=%ld bg=%d "
                                                "at=%d,%d expected=%d actual=%d bounds=%ld,%ld,%ld,%ld overhang=%ld\n",
                                                cases, L"font", style, horizontal, vertical, background, x, y,
                                                static_cast<byte *>(referencePixels)[y * width + x],
                                                actualPixels[y * pitch + x], expected.left, expected.top,
                                                expected.right, expected.bottom, metric.tmOverhang);
                                            return 1;
                                        }
                                for (int y = 0; y < height; ++y)
                                    for (int x = 0; x < width; ++x)
                                        static_cast<std::uint32_t *>(referenceRgbPixels)[y * width + x] =
                                            reinterpret_cast<std::uint32_t *>(rgbPixels + y * rgbPitch)[x] =
                                                ink ? 0x123456 : 0x646464;
                                rgbReference->DrawRgch(const_cast<achar *>(text), length, point, &drawing, &font);
                                rgbActual->DrawRgch(const_cast<achar *>(text), length, point, &drawing, &font);
                                GraphicsPort::Flush();
                                for (int y = 0; y < height; ++y)
                                    for (int x = 0; x < width; ++x)
                                        if ((static_cast<std::uint32_t *>(referenceRgbPixels)[y * width + x] &
                                             0xFFFFFF) !=
                                            (reinterpret_cast<std::uint32_t *>(rgbPixels + y * rgbPitch)[x] & 0xFFFFFF))
                                        {
                                            std::fprintf(
                                                stderr,
                                                "RGB font draw mismatch: case=%d style=%ld h=%ld v=%ld bg=%d at=%d,%d "
                                                "expected=%x actual=%x\n",
                                                cases, style, horizontal, vertical, background, x, y,
                                                static_cast<std::uint32_t *>(referenceRgbPixels)[y * width + x] &
                                                    0xFFFFFF,
                                                reinterpret_cast<std::uint32_t *>(rgbPixels + y * rgbPitch)[x] &
                                                    0xFFFFFF);
                                            return 1;
                                        }
                                ++cases;
                            }
        }
    }
    if (PackedText(1) || PackedText(4) || PackedText(16) || PackedText(24))
        return 1;
    _putenv_s("KAUAI_FONT_PROVIDER", "freetype");
    if (!std::strcmp(KauaiFonts::StudioFontProvider(), "freetype"))
    {
#ifdef KAUAI_SDL3
        // The portable font provider must also leave packed text independent
        // of a native device context, including the boxed-text frame.
        for (int packedDepth : {1, 4, 16, 24})
        {
            RC packedBounds(0, 0, 157, 91);
            auto packed = GraphicsPort::PgptNewOffscreen(&packedBounds, packedDepth);
            String packedFamily;
            packedFamily.SetSz(PszLit("Arial"));
            long packedNumber;
            if (!packed || !vntl.FGetOnn(&packedFamily, &packedNumber))
                return 1;
            auto packedPixels = packed->PrgbLockPixels();
            if (!packedPixels)
                return 1;
            const auto packedBytes = packed->CbRow() * packedBounds.Dyp();
            std::memset(packedPixels, 0, packedBytes);
            if (packedDepth == 16)
                for (int y = 0; y < packedBounds.Dyp(); ++y)
                    for (int x = 0; x < packedBounds.Dxp(); ++x)
                        packedPixels[y * packed->CbRow() + x * 2 + 1] = 0x80;
            FontDescription packedFont = {packedNumber, fontBold | fontItalic | (packedDepth == 1 ? 0 : fontBoxed), 16,
                                          tahLeft, tavTop};
            GraphicsDrawingData packedDrawing = {};
            packedDrawing.acrFore = AbstractColor(255, 255, 255);
            packedDrawing.acrBack = kacrClear;
            packed->DrawRgch(const_cast<achar *>(text), length, {8, 8}, &packedDrawing, &packedFont);
            if (packedDepth == 1)
            {
                packedDrawing.acrBack = AbstractColor(13, 98, 201);
                packed->DrawRgch(const_cast<achar *>(text), length, {8, 8}, &packedDrawing, &packedFont);
                GraphicsDrawingData fill = {};
                fill.grfgdd = fgddFill;
                fill.acrFore = AbstractColor(14, 99, 202);
                SystemRectangle swatch = {0, 0, 7, 1};
                packed->DrawRcs(&swatch, &fill);
                if (packedPixels[0] != 0xFE)
                    return 1;
                packedDrawing.acrBack = AbstractColor(255, 255, 255);
                packed->DrawRgch(const_cast<achar *>(text), length, {8, 8}, &packedDrawing, &packedFont);
                swatch = {0, 1, 7, 2};
                packed->DrawRcs(&swatch, &fill);
                if (packedPixels[packed->CbRow()] != 0xFE)
                    return 1;
                // Selecting the stock inversion brush invalidates the retained
                // solid realization; a new color brush sees the white background.
                fill.acrFore = kacrInvert;
                packed->DrawRcs(&swatch, &fill);
                fill.acrFore = AbstractColor(14, 99, 202);
                swatch = {0, 2, 7, 3};
                packed->DrawRcs(&swatch, &fill);
                if (packedPixels[2 * packed->CbRow()] != 0)
                    return 1;
            }
            bool packedInk = false;
            for (long i = 0; i < packedBytes; ++i)
                packedInk |= packedPixels[i] != 0;
            if (packedDepth == 16)
            {
                if (packedPixels[0] != 0 || packedPixels[1] != 0x80)
                    return 1;
                bool writtenInk = false;
                for (int y = 0; y < packedBounds.Dyp(); ++y)
                    for (int x = 0; x < packedBounds.Dxp(); ++x)
                    {
                        const auto pixel = packedPixels + y * packed->CbRow() + x * 2;
                        writtenInk |= !(pixel[1] & 0x80) && (pixel[0] || pixel[1]);
                    }
                if (!writtenInk)
                    return 1;
            }
            if (!packedInk || packed->FHasNativeContext() || packed->FHasNativeMirror())
                return 1;
            packed->Unlock();
            ReleasePpo(&packed);
        }
#endif
        int cells = 0;
        for (auto family :
             {"Arial", "Comic Sans MS", "Times New Roman", "Courier New", "Cambria", "Yu Gothic", "System"})
            for (int pixels : {9, 12, 16, 18, 24, 32})
                for (int style : {0, 1, 2, 3})
                {
                    auto sourceFont =
                        CreateFontA(-pixels, 0, 0, 0, style & 1 ? FW_BOLD : FW_NORMAL, (style & 2) != 0, FALSE, FALSE,
                                    DEFAULT_CHARSET, 0, 0, 0, 0, const_cast<char *>(family));
                    auto sourceDc = CreateCompatibleDC(nullptr);
                    if (!sourceFont || !sourceDc)
                        return 1;
                    SelectObject(sourceDc, sourceFont);
                    char selected[LF_FACESIZE] = {};
                    GetTextFaceA(sourceDc, LF_FACESIZE, selected);
                    if (!_stricmp(selected, family))
                    {
                        auto before = KauaiFonts::FontProviderStats();
                        auto portable = KauaiFonts::CreateStudioFace({family, std::strlen(family)}, pixels,
                                                                     (style & 1) != 0, (style & 2) != 0, false);
                        auto after = KauaiFonts::FontProviderStats();
                        TEXTMETRICA expected = {};
                        GetTextMetricsA(sourceDc, &expected);
                        KauaiFonts::Metrics actual;
                        if (!portable || !portable->Measure({"AV fj", 5}, actual) ||
                            after.nativeBootstraps != before.nativeBootstraps ||
                            after.portable != before.portable + 1 || actual.cellHeight != expected.tmHeight ||
                            actual.ascent != expected.tmAscent || actual.externalLeading != expected.tmExternalLeading)
                        {
                            std::fprintf(stderr,
                                         "File font metrics mismatch: %s pixels=%d style=%d expected=%ld,%ld,%ld "
                                         "actual=%d,%d,%d\n",
                                         family, pixels, style, expected.tmHeight, expected.tmAscent,
                                         expected.tmExternalLeading, actual.cellHeight, actual.ascent,
                                         actual.externalLeading);
                            return 1;
                        }
                        ++cells;
                    }
                    DeleteDC(sourceDc);
                    DeleteObject(sourceFont);
                }
        std::printf("File font selection: %d cell metric comparisons without native bootstrap passed.\n", cells);
        const wchar_t *portableFamilies[] = {L"Arial", L"System", L"Cambria", L"Yu Gothic"};
        for (auto family : portableFamilies)
        {
            auto sourceFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0,
                                          const_cast<wchar_t *>(family));
            HDC sourceDc = CreateCompatibleDC(nullptr);
            if (!sourceFont || !sourceDc)
                return 1;
            SelectObject(sourceDc, sourceFont);
            wchar_t selected[LF_FACESIZE] = {};
            GetTextFaceW(sourceDc, LF_FACESIZE, selected);
            if (!_wcsicmp(selected, family))
            {
                const auto before = KauaiFonts::FontProviderStats();
                auto portable = KauaiFonts::CreateStudioFace(sourceFont);
                const auto after = KauaiFonts::FontProviderStats();
                KauaiFonts::Metrics metrics;
                KauaiFonts::Mask glyphs;
                if (!portable || after.portable != before.portable + 1 || after.fallback != before.fallback ||
                    !portable->Measure({"AV fj\xE9", 6}, metrics) ||
                    !portable->Rasterize({"AV fj\xE9", 6}, glyphs, true) || glyphs.coverage.empty())
                    return 1;
            }
            DeleteDC(sourceDc);
            DeleteObject(sourceFont);
        }
    }
    actual->Unlock();
    rgbActual->Unlock();
    ReleasePpo(&actual);
    ReleasePpo(&reference);
    ReleasePpo(&rgbActual);
    ReleasePpo(&rgbReference);
    ReleasePpo(&region);
    SelectObject(rgbDc, previousRgb);
    DeleteObject(rgbBitmap);
    DeleteDC(rgbDc);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
    ReleasePpo(&active);
    ReleasePpo(&palette);
    std::printf("Font host: %d indexed and %d RGB native measurement/drawing comparisons passed.\n", cases, cases);
    return 0;
}
