/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstdio>
#include <cstring>
#include <vector>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
struct Dib
{
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    byte *pixels = nullptr;
    bool Init(int width, int height)
    {
        struct
        {
            BITMAPINFOHEADER header;
            RGBQUAD colors[256];
        } info = {};
        info.header.biSize = sizeof(BITMAPINFOHEADER);
        info.header.biWidth = width;
        info.header.biHeight = -height;
        info.header.biPlanes = 1;
        info.header.biBitCount = 8;
        info.header.biClrUsed = 256;
        dc = CreateCompatibleDC(nullptr);
        void *bits = nullptr;
        bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &bits, nullptr, 0);
        pixels = static_cast<byte *>(bits);
        if (!dc || !bitmap || !pixels)
            return false;
        previous = SelectObject(dc, bitmap);
        return true;
    }
    ~Dib()
    {
        if (previous)
            SelectObject(dc, previous);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
    }
};
int main()
{
    constexpr int width = 37, height = 29, pitch = 40, sourceWidth = 11, sourceHeight = 9, sourcePitch = 12;
    vwig.hdcApp = GetDC(nullptr);
    auto palette = DynamicArray::PglNew(sizeof(Color), 256);
    if (!vwig.hdcApp || !palette || !palette->FSetIvMac(256))
        return 1;
    Dib referenceDib, sourceDib;
    if (!referenceDib.Init(width, height) || !sourceDib.Init(sourceWidth, sourceHeight))
        return 1;
    auto reference = GraphicsPort::PgptNew(referenceDib.dc);
    auto referenceSource = GraphicsPort::PgptNew(sourceDib.dc);
    RC bounds(0, 0, width, height), sourceBounds(0, 0, sourceWidth, sourceHeight);
    auto actual = GraphicsPort::PgptNewOffscreen(&bounds, 8);
    auto actualSource = GraphicsPort::PgptNewOffscreen(&sourceBounds, 8);
    RC clip(2, 1, 35, 28), hole(9, 7, 19, 21);
    auto region = Region::PregnNew(&clip);
    if (!reference || !referenceSource || !actual || !actualSource || !region || !region->FDiffRc(&hole))
        return 1;
    for (auto port : {reference, actual})
    {
        region->AddRef();
        PRegion owned = region;
        port->ClipToRegn(&owned);
    }
    auto pixels = actual->PrgbLockPixels();
    auto sourcePixels = actualSource->PrgbLockPixels();
    if (!pixels || !sourcePixels || actual->CbRow() != pitch || actualSource->CbRow() != sourcePitch)
        return 1;
    for (int i = 0; i < sourcePitch * sourceHeight; ++i)
        sourceDib.pixels[i] = sourcePixels[i] = static_cast<byte>(i * 47);
    struct CopyCase
    {
        RC source, destination;
    };
    std::vector<CopyCase> copies;
    RC sources[] = {sourceBounds, RC(2, 1, 9, 7), RC(-2, -1, 9, 8), RC(3, 2, 13, 11)};
    for (const auto &source : sources)
        for (int h = 1; h <= 11; ++h)
            for (int w = 1; w <= 15; ++w)
                copies.push_back({source, RC(3, 2, 3 + w, 2 + h)});
    copies.push_back({sourceBounds, RC(-4, -2, 23, 24)});
    copies.push_back({sourceBounds, RC(23, 17, 43, 35)});
    int cases = 0;
    for (int sourcePass = 0; sourcePass < 3; ++sourcePass)
    {
        Color sourceColors[256], destinationColors[256];
        for (int i = 0; i < 256; ++i)
        {
            sourceColors[i] =
                sourcePass ? Color{static_cast<byte>(i), static_cast<byte>(i), static_cast<byte>(i), 0}
                           : Color{static_cast<byte>(i * 17), static_cast<byte>(i * 37), static_cast<byte>(i * 73), 0};
            if (sourcePass == 2 && (i == 17 || i == 200))
                sourceColors[i] = {255, 255, 255, 0};
            palette->Put(i, &sourceColors[i]);
        }
        actualSource->SetOffscreenColors(palette);
        SetDIBColorTable(sourceDib.dc, 0, 256, reinterpret_cast<RGBQUAD *>(sourceColors));
        for (int destinationPass = 0; destinationPass < 5; ++destinationPass)
        {
            for (int i = 0; i < 256; ++i)
            {
                destinationColors[i] =
                    destinationPass == 0   ? sourceColors[i]
                    : destinationPass == 1 ? sourceColors[(i + 137) & 255]
                    : destinationPass == 2
                        ? Color{static_cast<byte>(255 - i), static_cast<byte>(255 - i), static_cast<byte>(255 - i), 0}
                        : Color{static_cast<byte>(i * 73), static_cast<byte>(i * 17), static_cast<byte>(i * 37), 0};
                if (destinationPass == 4 && (i == 17 || i == 200))
                    destinationColors[i] = {128, 128, 128, 0};
                palette->Put(i, &destinationColors[i]);
            }
            actual->SetOffscreenColors(palette);
            SetDIBColorTable(referenceDib.dc, 0, 256, reinterpret_cast<RGBQUAD *>(destinationColors));
            for (int self = 0; self < 2; ++self)
            {
                CopyCase selfCopies[] = {{RC(2, 1, 30, 24), RC(4, 3, 32, 26)},
                                         {RC(4, 3, 32, 26), RC(2, 1, 30, 24)},
                                         {RC(0, 0, 11, 9), RC(4, 3, 31, 26)},
                                         {RC(0, 0, 31, 26), RC(4, 3, 15, 12)}};
                const auto count = self ? sizeof(selfCopies) / sizeof(selfCopies[0]) : copies.size();
                for (std::size_t index = 0; index < count; ++index)
                {
                    auto copy = self ? selfCopies[index] : copies[index];
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < pitch; ++x)
                            referenceDib.pixels[y * pitch + x] = pixels[y * pitch + x] =
                                static_cast<byte>(y * 17 + x * 31);
                    SystemRectangle nativeSource = copy.source, nativeDestination = copy.destination, nativeClip = clip;
                    GraphicsDrawingData drawing = {};
                    RC graphicsClip;
                    drawing.prcsClip = &(graphicsClip = RC(nativeClip));
                    reference->CopyPixels(self ? reference : referenceSource, &nativeSource, &nativeDestination,
                                          &drawing);
                    actual->CopyPixels(self ? actual : actualSource, &nativeSource, &nativeDestination, &drawing);
                    GraphicsPort::Flush();
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < pitch; ++x)
                            if (referenceDib.pixels[y * pitch + x] != pixels[y * pitch + x])
                            {
                                std::fprintf(stderr,
                                             "Indexed copy mismatch: source=%d destination=%d self=%d case=%zu "
                                             "at=%d,%d expected=%u actual=%u\n",
                                             sourcePass, destinationPass, self, index, x, y,
                                             referenceDib.pixels[y * pitch + x], pixels[y * pitch + x]);
                                return 1;
                            }
                    ++cases;
                }
            }
        }
    }
    actual->Unlock();
    actualSource->Unlock();
    ReleasePpo(&actualSource);
    ReleasePpo(&actual);
    ReleasePpo(&referenceSource);
    ReleasePpo(&reference);
    ReleasePpo(&region);
    ReleasePpo(&palette);
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
    std::printf("Indexed bitmap copies: %d native/raw-index comparisons passed.\n", cases);
    return 0;
}
