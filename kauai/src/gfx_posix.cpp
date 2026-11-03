/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// CPU-owned GraphicsPort backend. No device context or desktop operation.
#include "pixel_buffer.h"
#include "palette_state.h"
#include "sdl_host.h"
#include "font_host.h"
#include <cstring>
#include <memory>
#include <new>
#include "frame.h"
#undef size
ASSERTNAME
namespace
{
KauaiPixels::PaletteState palette;
std::uint64_t paletteVersion = 1;
} // namespace
Color *GraphicsPort::_prgclr;
LegacyLong GraphicsPort::_cclrPal;

PGraphicsPort GraphicsPort::PgptNewOffscreen(RC *bounds, long depth)
{
    if (!bounds || depth < 1 || depth > 32)
        return nullptr;
    const auto width = std::int64_t(bounds->xpRight) - bounds->xpLeft;
    const auto height = std::int64_t(bounds->ypBottom) - bounds->ypTop;
    if (width <= 0 || height <= 0 || width > INT32_MAX || height > INT32_MAX)
        return nullptr;
    // Studio installs its palette before requesting indexed drawing surfaces.
    // There is no host device palette to borrow on POSIX.
    if ((depth == 4 || depth == 8) && (!_prgclr || !_cclrPal))
        return nullptr;
    std::unique_ptr<KauaiPixels::PixelBuffer> storage(new (std::nothrow) KauaiPixels::PixelBuffer);
    if (!storage || !storage->Init(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                                   static_cast<std::int32_t>(depth)))
        return nullptr;
    auto port = NewObj GraphicsPort;
    if (!port)
        return nullptr;
    port->_pixelBuffer = storage.release();
    port->_prgbPixels = static_cast<byte *>(port->_pixelBuffer->Pixels());
    port->_cbitPixel = static_cast<LegacyLong>(depth);
    port->_cbRow = port->_pixelBuffer->Pitch();
    port->_ptBase = bounds->PtTopLeft();
    port->_rcOff.Set(0, 0, static_cast<LegacyLong>(width), static_cast<LegacyLong>(height));
    port->_rcClip.Max();
    if (depth == 1)
        port->_rgclrPixels[1] = {255, 255, 255, 0};
    port->_EnsurePalette();
    return port;
}
GraphicsPort::~GraphicsPort()
{
    Assert(_cactLock == 0, "pixels are still locked");
    KauaiSDL::Main().UnbindPixels(_prgbPixels);
    delete _fontFace;
    delete _pixelBuffer;
    ReleasePpo(&_pregnClip);
}
void GraphicsPort::Flush()
{
    // CPU writes complete synchronously; there is no native drawing queue.
}
void GraphicsPort::SetActiveColors(PDynamicArray colors, ulong flags)
{
    AssertNilOrPo(colors, 0);
    if (colors && colors->CbEntry() != sizeof(Color))
    {
        PushErc(ercGfxCantSetPalette);
        return;
    }
    if (!_prgclr && !FAllocPv(reinterpret_cast<void **>(&_prgclr), 256 * sizeof(Color), fmemClear, mprNormal))
    {
        PushErc(ercGfxCantSetPalette);
        return;
    }
    const auto count = colors ? colors->IvMac() : 0;
    const auto change = palette.Set(count ? static_cast<const Color *>(colors->QvGet(0)) : nullptr,
                                    static_cast<std::size_t>(count), static_cast<std::uint32_t>(flags));
    if (change != KauaiPixels::PaletteChange::changed)
        return;
    std::memcpy(_prgclr, palette.Colors(), 256 * sizeof(Color));
    _cclrPal = static_cast<LegacyLong>(palette.Count());
    ++paletteVersion;
    ++vcactRealize;
}
PDynamicArray GraphicsPort::PglclrGetPalette()
{
    auto colors = DynamicArray::PglNew(sizeof(Color), _cclrPal);
    if (!colors || !colors->FSetIvMac(_cclrPal))
    {
        ReleasePpo(&colors);
        return nullptr;
    }
    if (_cclrPal)
        std::memcpy(colors->QvGet(0), _prgclr, _cclrPal * sizeof(Color));
    return colors;
}
void GraphicsPort::_EnsurePalette()
{
    if (_pixelPaletteVersion == paletteVersion)
        return;
    if (!_pixelOwnPalette && _prgclr && _cbitPixel > 1 && _cbitPixel <= 8)
    {
        const auto count = LwMin(_cclrPal, 1L << _cbitPixel);
        std::memcpy(_rgclrPixels, _prgclr, count * sizeof(Color));
    }
    _pixelPaletteVersion = paletteVersion;
}
void GraphicsPort::SetOffscreenColors(PDynamicArray colors)
{
    AssertThis(0);
    if (!colors)
    {
        _pixelOwnPalette = false;
        _pixelPaletteVersion = 0;
        return;
    }
    if (colors->CbEntry() != sizeof(Color))
    {
        PushErc(ercGfxCantSetPalette);
        return;
    }
    if (_cbitPixel > 1 && _cbitPixel <= 8)
    {
        const auto count = LwMin(colors->IvMac(), 1L << _cbitPixel);
        if (count)
            std::memcpy(_rgclrPixels, colors->QvGet(0), count * sizeof(Color));
        _pixelOwnPalette = true;
    }
}
byte *GraphicsPort::PrgbLockPixels(RC *bounds)
{
    AssertThis(0);
    if (bounds)
        *bounds = _rcOff;
    Lock();
    return _prgbPixels;
}
long GraphicsPort::CbRow()
{
    return _cbRow;
}
long GraphicsPort::CbitPixel()
{
    return _cbitPixel;
}
void GraphicsPort::Lock()
{
    ++_cactLock;
}
void GraphicsPort::Unlock()
{
    if (--_cactLock < 0)
    {
        Bug("Unmatched unlock call");
        _cactLock = 0;
    }
}
std::uint32_t GraphicsPort::_PixelBackgroundColor() const
{
    return _monoBackground;
}
byte GraphicsPort::_PixelColorReference(AbstractColor color)
{
    if (color == kacrClear || color == kacrInvert)
        return 0;
    if (B3Lw(color._lu) == kbIndexAcr)
        return _prgclr && B0Lw(color._lu) < _cclrPal ? 0 : 1;
    return 2; // Original PALETTERGB reference for an explicit RGB brush.
}
void GraphicsPort::DrawRcs(SystemRectangle *rectangle, GraphicsDrawingData *drawing)
{
    AssertThis(0);
    if (!_FDrawPixelRect(rectangle, drawing))
        PushErc(ercGfxCantDraw);
}
void GraphicsPort::HiliteRcs(SystemRectangle *rectangle, GraphicsDrawingData *drawing)
{
    AssertThis(0);
    if (!_FDrawPixelRect(rectangle, drawing, true))
        PushErc(ercGfxCantDraw);
}
void GraphicsPort::DrawOval(SystemRectangle *rectangle, GraphicsDrawingData *drawing)
{
    AssertThis(0);
    if (!_FDrawPixelOval(rectangle, drawing))
        PushErc(ercGfxCantDraw);
}
void GraphicsPort::DrawPoly(HQ polygon, GraphicsDrawingData *drawing)
{
    AssertThis(0);
    AssertHq(polygon);
    auto shape = static_cast<const OLY *>(PvLockHq(polygon));
    const bool drawn = _FDrawPixelPolygon(shape, drawing);
    UnlockHq(polygon);
    if (!drawn)
        PushErc(ercGfxCantDraw);
}
void GraphicsPort::DrawLine(SystemPoint *first, SystemPoint *last, GraphicsDrawingData *drawing)
{
    AssertThis(0);
    alignas(OLY) byte storage[kcbOlyBase + 6 * sizeof(SystemPoint)];
    auto polygon = reinterpret_cast<OLY *>(storage);
    BuildLinePolygon(polygon, *first, *last, drawing->dxpPen, drawing->dypPen);
    if (!_FDrawPixelPolygon(polygon, drawing))
        PushErc(ercGfxCantDraw);
}
#ifdef DEBUG
void GraphicsPort::AssertValid(LegacyUlong)
{
    GraphicsPort_PAR::AssertValid(0);
    Assert(_pixelBuffer && _pixelBuffer->Pixels() == _prgbPixels, "invalid CPU pixel storage");
    Assert(_pixelBuffer->Pitch() == _cbRow && _pixelBuffer->Depth() == _cbitPixel, "invalid CPU pixel layout");
}
void GraphicsPort::MarkStaticMem()
{
    MarkPv(_prgclr);
}
#endif

bool GraphicsPort::_PixelOwnPalette() const
{
    return _pixelOwnPalette;
}
void GraphicsPort::_SetPixelBackgroundColor(std::uint32_t color)
{
    _monoBackground = color;
}
void GraphicsPort::CopyPixels(PGraphicsPort source, SystemRectangle *sourceRect, SystemRectangle *destinationRect,
                              GraphicsDrawingData *drawing)
{
    AssertThis(0);
    AssertPo(source, 0);
    if (!_FCopyPixels(source, sourceRect, destinationRect, drawing))
        PushErc(ercGfxCantDraw);
}
void GraphicsPort::ScrollRcs(SystemRectangle *rectangle, long dx, long dy, GraphicsDrawingData *drawing)
{
    AssertThis(0);
    if (!_FScrollPixels(rectangle, dx, dy, drawing))
        PushErc(ercGfxCantDraw);
}
void GraphicsPort::DrawMbmp(PMaskedBitmapMBMP bitmap, SystemRectangle *rectangle, GraphicsDrawingData *drawing)
{
    AssertThis(0);
    AssertPo(bitmap, 0);
    if (!_FDrawPixelMbmp(bitmap, rectangle, drawing))
        PushErc(ercGfxCantDraw);
}

bool GraphicsPort::_FEnsureTextFace(FontDescription *font)
{
    return _FPortableTextFace(font);
}
void GraphicsPort::DrawRgch(achar *text, long count, SystemPoint point, GraphicsDrawingData *drawing,
                            FontDescription *font)
{
    AssertThis(0);
    AssertPo(font, 0);
    if (!_FDrawPixelText(text, count, point, drawing, font))
        PushErc(ercGfxCantDraw);
}
void GraphicsPort::GetRcsFromRgch(SystemRectangle *rectangle, achar *text, long count, SystemPoint point,
                                  FontDescription *font)
{
    AssertThis(0);
    AssertPo(font, 0);
    KauaiFonts::Placement placement;
    if (_FHostText(text, count, point, font, placement))
        *rectangle = {placement.logical.left, placement.logical.top, placement.logical.right, placement.logical.bottom};
    else
    {
        *rectangle = {};
        PushErc(ercGfxCantSetFont);
    }
}
bool FontList::FInit()
{
    return _FInitPortable();
}
bool FontList::FFixedPitch(long index)
{
    AssertThis(0);
    Assert(FValidOnn(index), "bad font index");
    LegacyLong flags;
    _pgst->GetExtra(index, &flags);
    return (flags & 1) != 0;
}

PGraphicsPort GraphicsPort::PgptNewHwnd(CoreNative::WindowHandle window)
{
    return PgptNewSDLWindow(window);
}
