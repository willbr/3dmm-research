/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Masked bitmap routines that a tool might use.

***************************************************************************/
#include "kauai_core.h"
#include "region.h"
#include "mbmp.h"
#include "bitmap_format.h"
#include "three_mm/bitmap_file.h"
ASSERTNAME

RTCLASS(MaskedBitmapMBMP)

/***************************************************************************
    Destructor for a masked bitmap.
***************************************************************************/
MaskedBitmapMBMP::~MaskedBitmapMBMP(void)
{
    AssertBaseThis(0);
    FreePhq(&_hqrgb);
}

/***************************************************************************
    Static method to create a new MaskedBitmapMBMP based on the given prgbPixels with
    extracting rectangle *prc and transparent color bTransparent.  prgbPixels
    should be a two-dimensional matrix of 8-bit pixels with width cbRow and
    height dyp.  prc should be the desired extracting rectangle.  xp and yp
    are the coordinates for the reference point of the MaskedBitmapMBMP (with (0,0) being
    the upper-left corner).  bTransparent should be the pixel value for the
    transparent color for the MaskedBitmapMBMP.
***************************************************************************/
PMaskedBitmapMBMP MaskedBitmapMBMP::PmbmpNew(byte *prgbPixels, LegacyLong cbRow, LegacyLong dyp, RC *prc, LegacyLong xpRef, LegacyLong ypRef, byte bTransparent,
                     LegacyUlong grfmbmp, byte bDefault)
{
    AssertIn(cbRow, 1, kcbMax);
    AssertIn(dyp, 1, kcbMax);
    AssertPvCb(prgbPixels, LwMul(cbRow, dyp));
    AssertVarMem(prc);
    PMaskedBitmapMBMP pmbmp;

    if (pvNil != (pmbmp = NewObj MaskedBitmapMBMP) &&
        !pmbmp->_FInit(prgbPixels, cbRow, dyp, prc, xpRef, ypRef, bTransparent, grfmbmp, bDefault))
    {
        ReleasePpo(&pmbmp);
    }
    return pmbmp;
}

/***************************************************************************
    Initialize the MaskedBitmapMBMP based on the given pixels.
***************************************************************************/
bool MaskedBitmapMBMP::_FInit(byte *prgbPixels, LegacyLong cbRow, LegacyLong dyp, RC *prc, LegacyLong xpRef, LegacyLong ypRef, byte bTransparent,
                  LegacyUlong grfmbmp, byte bDefault)
{
    AssertIn(cbRow, 1, kcbMax);
    AssertIn(dyp, 1, kcbMax);
    AssertPvCb(prgbPixels, LwMul(cbRow, dyp));
    AssertVarMem(prc);
    Assert(!prc->FEmpty() && prc->xpLeft >= 0 && prc->xpRight <= cbRow && prc->ypTop >= 0 && prc->ypBottom <= dyp,
           "Invalid rectangle");
    short *qrgcb;
    byte *pb, *pbRow, *pbLimRow;
    byte *qbDst, *qbDstPrev;
    LegacyLong cbPixelData, cbPrev, cbOpaque, cbRun;
    LegacyLong dibRow;
    bool fTrans, fMask;
    LegacyLong xpMin, xpLim, ypLim, xp, yp;
    RC rc = *prc;

    // allocate enough space for the rgcb
    if (!FAllocHq(&_hqrgb, size(MaskedBitmapOnFile) + LwMul(rc.Dyp(), size(short)), fmemNil, mprNormal))
    {
        return fFalse;
    }

    _Qmbmph()->fMask = fMask = FPure(grfmbmp & fmbmpMask);
    _Qmbmph()->bFill = bDefault;
    dibRow = (grfmbmp & fmbmpUpsideDown) ? -cbRow : cbRow;

    // crop the bitmap and get an upper bound on the size of the pixel data
    pbRow = prgbPixels + LwMul(cbRow, ((grfmbmp & fmbmpUpsideDown) ? dyp - rc.ypTop - 1 : rc.ypTop));
    qrgcb = _Qrgcb();
    xpMin = rc.xpRight;
    xpLim = rc.xpLeft;
    ypLim = rc.ypTop;
    cbPixelData = 0;
    for (yp = rc.ypTop; yp < rc.ypBottom; pbRow += dibRow, yp++)
    {
        pb = pbRow + rc.xpLeft;
        pbLimRow = pbRow + rc.xpRight;
        cbPrev = cbPixelData;
        cbRun = cbOpaque = 0;
        fTrans = fTrue;
        for (;;)
        {
            // check for overflow or change in transparent status, or the
            // end of the row
            if (pb == pbLimRow || fTrans != (*pb == bTransparent) || cbRun == kbMax)
            {
                if (fTrans && pb == pbLimRow)
                    break;
                cbPixelData++;
                if (!fTrans && cbRun > 0)
                {
                    xp = pb - pbRow;
                    if (xpMin > xp - cbRun)
                        xpMin = xp - cbRun;
                    if (xpLim < xp)
                        xpLim = xp;
                    cbOpaque += cbRun;
                    if (!fMask)
                        cbPixelData += cbRun;
                }
                if (pb == pbLimRow)
                    break;
                cbRun = 0;
                fTrans = !fTrans;
            }
            else
            {
                // Increment pixel count and go on to next pixel.
                cbRun++;
                pb++;
            }
        }

        if (0 == cbOpaque)
        {
            // nothing in this row but transparent pixels
            if (yp == rc.ypTop)
                rc.ypTop++;
            else
                qrgcb[yp - rc.ypTop] = 0;
            cbPixelData = cbPrev;
        }
        else
        {
            // set the row length in the rgcb.
            AssertIn(cbPixelData - cbPrev, 2 + !fMask, kswMax + 1);
            qrgcb[yp - rc.ypTop] = (short)(cbPixelData - cbPrev);
            ypLim = yp + 1;
        }
    }
    rc.ypBottom = ypLim;
    rc.xpLeft = xpMin;
    rc.xpRight = xpLim;
    if (rc.FEmpty())
    {
        Warn("Empty source bitmap for MaskedBitmapMBMP");
        rc.Zero();
    }

    // reallocate the _hqrgb to the size actually needed
    AssertIn(LwMul(rc.Dyp(), size(short)), 0, CbOfHq(_hqrgb) - size(MaskedBitmapOnFile) + 1);

    _cbRgcb = LwMul(rc.Dyp(), size(short));
    if (!FResizePhq(&_hqrgb, _cbRgcb + size(MaskedBitmapOnFile) + cbPixelData, fmemNil, mprNormal))
    {
        return fFalse;
    }

    // now actually construct the pixel data
    qrgcb = _Qrgcb();
    qbDst = (byte *)PvAddBv(qrgcb, _cbRgcb);
    pbRow = prgbPixels + LwMul(cbRow, ((grfmbmp & fmbmpUpsideDown) ? dyp - rc.ypTop - 1 : rc.ypTop));
    for (yp = rc.ypTop; yp < rc.ypBottom; pbRow += dibRow, yp++)
    {
        if (qrgcb[yp - rc.ypTop] == 0)
        {
            // empty row, no need to scan it
            AssertIn(yp, rc.ypTop + 1, rc.ypBottom);
            continue;
        }

        pb = pbRow + rc.xpLeft;
        pbLimRow = pbRow + rc.xpRight;
        qbDstPrev = qbDst;
        cbRun = 0;
        fTrans = fTrue;
        for (;;)
        {
            // check for overflow or change in transparent status, or the
            // end of the row
            if (pb == pbLimRow || fTrans != (*pb == bTransparent) || cbRun == kbMax)
            {
                if (fTrans && pb == pbLimRow)
                    break;
                *qbDst++ = (byte)cbRun;
                if (!fTrans)
                {
                    if (!fMask)
                    {
                        CopyPb(pb - cbRun, qbDst, cbRun);
                        qbDst += cbRun;
                    }
                    if (pb == pbLimRow)
                        break;
                }
                cbRun = 0;
                fTrans = !fTrans;
            }
            else
            {
                // Increment pixel count and go on to next pixel.
                cbRun++;
                pb++;
            }
        }

        // Set the row length in the rgcb.
        cbRun = qbDst - qbDstPrev;
        AssertIn(cbRun, 2 + !fMask, qrgcb[yp - rc.ypTop] + 1);
        qrgcb[yp - rc.ypTop] = (short)cbRun;
    }

    // shrink _hqrgb to the actual size needed
    cbRun = BvSubPvs(qbDst, QvFromHq(_hqrgb));
    AssertIn(cbRun, 0, CbOfHq(_hqrgb) + 1);
    AssertDo(FResizePhq(&_hqrgb, cbRun, fmemNil, mprNormal), "shrinking failed!");

    // set the bounding rectangle of the MaskedBitmapMBMP
    rc.Offset(-xpRef, -ypRef);
    _Qmbmph()->rc = rc;

    AssertThis(0);
    return fTrue;
}

/****************************************************************************
    This function will read in a bitmap file and return a PMaskedBitmapMBMP made from it.
    (xp, yp) will be the reference point of the mbmp [(0,0) is uppper-left].
    All pixels with the same value as bTransparent will be read in as
    transparent. The bitmap file must be uncompressed and have a bit depth
    of 8.  The palette information is be ignored.
****************************************************************************/
PMaskedBitmapMBMP MaskedBitmapMBMP::PmbmpReadNative(Filename *pfni, byte bTransparent, LegacyLong xp, LegacyLong yp, LegacyUlong grfmbmp, byte bDefault)
{
    AssertPo(pfni, ffniFile);
    byte *prgb;
    RC rc;
    LegacyLong dxp, dyp;
    bool fUpsideDown;
    PMaskedBitmapMBMP pmbmp = pvNil;

    if (!FReadBitmap(pfni, &prgb, pvNil, &dxp, &dyp, &fUpsideDown, bTransparent))
        return pvNil;

    rc.Set(0, 0, dxp, dyp);
    pmbmp = MaskedBitmapMBMP::PmbmpNew(prgb, CbRoundToLong(rc.xpRight), rc.ypBottom, &rc, xp, yp, bTransparent,
                           (fUpsideDown ? grfmbmp : grfmbmp ^ fmbmpUpsideDown), bDefault);
    FreePpv((void **)&prgb);

    return pmbmp;
}

/***************************************************************************
    Read a masked bitmap from a block.  May free the block or modify it.
***************************************************************************/
PMaskedBitmapMBMP MaskedBitmapMBMP::PmbmpRead(PDataBlock pblck)
{
    AssertPo(pblck, 0);
    PMaskedBitmapMBMP pmbmp;
    MaskedBitmapOnFile *qmbmph;
    LegacyLong cbRgcb;
    LegacyLong cbTot;
    bool fSwap;
    RC rc;
    HQ hqrgb = hqNil;

    if (!pblck->FUnpackData())
        return pvNil;
    cbTot = pblck->Cb();

    if (cbTot < size(MaskedBitmapOnFile) || hqNil == (hqrgb = pblck->HqFree()))
        return pvNil;

    qmbmph = (MaskedBitmapOnFile *)QvFromHq(hqrgb);
    fSwap = (kboOther == qmbmph->bo);
    if (fSwap)
        SwapBytesBom(qmbmph, kbomMbmph);
    else if (qmbmph->bo != kboCur)
        goto LFail;

    if (qmbmph->swReserved != 0 || qmbmph->cb != cbTot)
        goto LFail;

    rc = qmbmph->rc;
    if (rc.FEmpty())
    {
        if (cbTot != size(MaskedBitmapOnFile))
            goto LFail;
        qmbmph->rc.xpRight = rc.xpLeft;
        qmbmph->rc.ypBottom = rc.ypTop;
    }

    cbRgcb = LwMul(rc.Dyp(), size(short));
    if (size(MaskedBitmapOnFile) + cbRgcb > cbTot)
        goto LFail;

    if (pvNil == (pmbmp = NewObj MaskedBitmapMBMP))
    {
    LFail:
        FreePhq(&hqrgb);
        return pvNil;
    }

    pmbmp->_cbRgcb = cbRgcb;
    pmbmp->_hqrgb = hqrgb;

    if (fSwap)
    {
        // swap bytes in the rgcb
        SwapBytesRgsw(pmbmp->_Qrgcb(), rc.Dyp());
    }

#ifdef DEBUG
    // verify the rgcb and rgb
    LegacyLong ccb, cb, dxp;
    byte *qb;
    bool fMask = pmbmp->_Qmbmph()->fMask;
    short *qcb = pmbmp->_Qrgcb();
    byte *qbRow = (byte *)PvAddBv(qcb, cbRgcb);

    cbTot -= size(MaskedBitmapOnFile) + cbRgcb;
    for (ccb = rc.Dyp(); ccb-- > 0;)
    {
        cb = *qcb++;
        if (!FIn(cb, 0, cbTot + 1))
            goto LFailDebug;
        cbTot -= cb;
        qb = qbRow;
        dxp = 0;
        while (qb < qbRow + cb)
        {
            dxp += *qb++;
            if (qb >= qbRow + cb)
                break;
            dxp += *qb;
            if (fMask)
                qb++;
            else
                qb += *qb + 1;
        }
        if (dxp > rc.Dxp() || qb != qbRow + cb)
            goto LFailDebug;
        qbRow = qb;
    }
    if (cbTot != 0)
    {
    LFailDebug:
        Bug("Attempted to read bad MaskedBitmapMBMP");
        ReleasePpo(&pmbmp);
    }
#endif // DEBUG

    AssertNilOrPo(pmbmp, 0);
    return pmbmp;
}

/***************************************************************************
    Return the total size on file.
***************************************************************************/
LegacyLong MaskedBitmapMBMP::CbOnFile(void)
{
    AssertThis(0);
    return CbOfHq(_hqrgb);
}

/***************************************************************************
    Write the masked bitmap (and its header) to the given block.
***************************************************************************/
bool MaskedBitmapMBMP::FWrite(PDataBlock pblck)
{
    AssertThis(0);
    AssertPo(pblck, 0);
    MaskedBitmapOnFile *qmbmph;

    qmbmph = _Qmbmph();
    qmbmph->bo = kboCur;
    qmbmph->osk = koskCur;
    qmbmph->swReserved = 0;
    qmbmph->cb = CbOfHq(_hqrgb);

    if (qmbmph->cb != pblck->Cb())
    {
        Bug("Wrong sized block");
        return fFalse;
    }

    if (!pblck->FWriteHq(_hqrgb, 0))
        return fFalse;

    return fTrue;
}

/***************************************************************************
    Get the natural rectangle for the mbmp.
***************************************************************************/
void MaskedBitmapMBMP::GetRc(RC *prc)
{
    AssertThis(0);
    *prc = _Qmbmph()->rc;
}

/***************************************************************************
    Return whether the given (xp, yp) is in a non-transparent pixel of
    the MaskedBitmapMBMP.  (xp, yp) should be given in MaskedBitmapMBMP coordinates.
***************************************************************************/
bool MaskedBitmapMBMP::FPtIn(LegacyLong xp, LegacyLong yp)
{
    AssertThis(0);
    byte *qb, *qbLim;
    short *qcb;
    short cb;
    MaskedBitmapOnFile *qmbmph;

    qmbmph = _Qmbmph();
    if (!qmbmph->rc.FPtIn(xp, yp))
        return fFalse;

    qcb = _Qrgcb();
    qb = (byte *)PvAddBv(qcb, _cbRgcb);
    for (yp -= qmbmph->rc.ypTop; yp-- > 0;)
        qb += *qcb++;

    qbLim = qb + *qcb;
    for (xp -= qmbmph->rc.xpLeft; qb < qbLim;)
    {
        if (0 > (xp -= *qb++) || qb >= qbLim)
            break;
        cb = *qb++;
        if (0 > (xp -= cb))
            return fTrue;
        if (!qmbmph->fMask)
            qb += cb;
    }
    Assert(qb <= qbLim, "bad row in MaskedBitmapMBMP");
    return fFalse;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a MaskedBitmapMBMP.
***************************************************************************/
void MaskedBitmapMBMP::AssertValid(LegacyUlong grf)
{
    LegacyLong ccb;
    LegacyLong cbTot;
    short *qcb;
    RC rc;

    MaskedBitmapMBMP_PAR::AssertValid(0);
    AssertHq(_hqrgb);

    rc = _Qmbmph()->rc;
    ccb = rc.Dyp();
    Assert(_cbRgcb == LwMul(rc.Dyp(), size(short)), "_cbRgcb wrong");
    cbTot = 0;
    qcb = _Qrgcb();
    while (ccb-- > 0)
    {
        AssertIn(*qcb, 0, kcbMax);
        cbTot += *qcb++;
    }
    Assert(cbTot + _cbRgcb + size(MaskedBitmapOnFile) == CbOfHq(_hqrgb), "_hqrgb wrong size");
}

/***************************************************************************
    Mark memory for the MaskedBitmapMBMP.
***************************************************************************/
void MaskedBitmapMBMP::MarkMem(void)
{
    AssertValid(0);
    MaskedBitmapMBMP_PAR::MarkMem();
    MarkHq(_hqrgb);
}
#endif // DEBUG

/***************************************************************************
    A PFNRPO to read an MaskedBitmapMBMP.
***************************************************************************/
bool MaskedBitmapMBMP::FReadMbmp(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, fblckReadable);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);
    PMaskedBitmapMBMP pmbmp;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;

    if (!pblck->FUnpackData())
        goto LFail;
    *pcb = pblck->Cb();

    if (pvNil == (pmbmp = PmbmpRead(pblck)))
    {
    LFail:
        TrashVar(ppbaco);
        TrashVar(pcb);
        return fFalse;
    }
    *ppbaco = pmbmp;
    return fTrue;
}

/***************************************************************************
    Given an Filename refering to a bitmap file, returns the interesting parts of
    the header and the pixel data and palette.  Fails if the bitmap is not
    8 bits, uncompressed.  Any or all of the output pointers may be nil.

    input:
        pfni	--	the file from which to read the data

    output:
        The following variable parameters are valid only if FReadBitmap
        returns true (*pprgb and *ppglclr will be nil if this fails):
            pprgb			-- pointer to the allocated memory for pixel data
            ppglclr			-- the palette
            pdxp			-- the width of the bitmap
            pdyp			-- the height of the bitmap
            pfUpsideDown	--	fTrue if the bitmap is upside down
        returns fTrue if it succeeds
***************************************************************************/
namespace
{
struct BitmapContext
{
    byte **pixels = nullptr;
    PDynamicArray *palette = nullptr;
    LegacyLong *width = nullptr, *height = nullptr;
    bool *upside = nullptr;
};
ThreeMMBitmapServices BitmapServices(BitmapContext *context)
{
    ThreeMMBitmapServices s = {};
    s.context = context;
    s.open = [](void *, void *p) -> void * { return FileObject::PfilOpen(static_cast<PFilename>(p)); };
    s.create = [](void *, void *p) -> void * { return FileObject::PfilCreate(static_cast<PFilename>(p)); };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.size = [](void *, void *p) -> ThreeMMI32 { return static_cast<PFileObject>(p)->FpMac(); };
    s.read = [](void *, void *p, void *out, ThreeMMI32 n, ThreeMMI32 at) -> int {
        return static_cast<PFileObject>(p)->FReadRgb(out, n, at);
    };
    s.write = [](void *, void *p, const void *data, ThreeMMI32 n, ThreeMMI32 at) -> int {
        return static_cast<PFileObject>(p)->FWriteRgb(const_cast<void *>(data), n, at);
    };
    s.allocate = [](void *, ThreeMMI32 n, void **out) -> int { return FAllocPv(out, n, fmemNil, mprNormal); };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.palette_create = [](void *) -> void * {
        auto palette = DynamicArray::PglNew(sizeof(Color), 256);
        if (palette)
            AssertDo(palette->FSetIvMac(256), "Ensured by creation");
        return palette;
    };
    s.palette_lock = [](void *, void *p) -> void * { return static_cast<PDynamicArray>(p)->PvLock(0); };
    s.palette_unlock = [](void *, void *p) { static_cast<PDynamicArray>(p)->Unlock(); };
    s.publish = [](void *p, int kind, void *object) {
        auto c = static_cast<BitmapContext *>(p);
        if (kind == THREE_MM_BITMAP_PIXELS)
            *c->pixels = static_cast<byte *>(object);
        else
            *c->palette = static_cast<PDynamicArray>(object);
    };
    s.dimensions = [](void *p, int valid, ThreeMMI32 w, ThreeMMI32 h, int up) {
        auto c = static_cast<BitmapContext *>(p);
        if (valid)
        {
            if (c->width)
                *c->width = w;
            if (c->height)
                *c->height = h;
            if (c->upside)
                *c->upside = up != 0;
        }
        else
        {
            TrashVar(c->width);
            TrashVar(c->height);
            TrashVar(c->upside);
        }
    };
    s.warning = [](void *, int reason) {
        PZString messages[] = {PszLit("bad bitmap file"),
                               PszLit("not an 8-bit bitmap"),
                               PszLit("bad compression type or bitmap file is wrong length"),
                               PszLit("Empty bitmap rectangle"),
                               PszLit("Bitmap data wrong size"),
                               PszLit("palette is wrong size"),
                               PszLit("compressed bitmap is bad")};
        Warn(messages[reason]);
    };
    s.bad_bitmap = [](void *) { PushErc(ercMbmpCantOpenBitmap); };
    s.make_temporary = [](void *, void *p) { static_cast<PFileObject>(p)->SetTemp(); };
    return s;
}
} // namespace
bool FReadBitmap(Filename *filename, byte **pixels, PDynamicArray *palette, LegacyLong *width, LegacyLong *height,
                 bool *upside, byte transparent)
{
    AssertPo(filename, ffniFile);
    AssertNilOrVarMem(pixels);
    AssertNilOrVarMem(palette);
    AssertNilOrVarMem(width);
    AssertNilOrVarMem(height);
    AssertNilOrVarMem(upside);
#ifndef MAC
    BitmapContext context;
    context.pixels = pixels;
    context.palette = palette;
    context.width = width;
    context.height = height;
    context.upside = upside;
    auto s = BitmapServices(&context);
    return three_mm_bitmap_read(filename, pixels != nullptr, palette != nullptr, transparent, &s) != 0;
#else
    if (pixels)
        *pixels = nullptr;
    if (palette)
        *palette = nullptr;
    TrashVar(width);
    TrashVar(height);
    TrashVar(upside);
    RawRtn();
    return false;
#endif
}

/***************************************************************************
    Writes a given bitmap to a given file.

    Arguments:
        Filename *pfni         -- the name of the file to write
        byte *prgb        -- the bits in the bitmap
        PDynamicArray pglclr        -- the palette of the bitmap
        long dxp          -- the width of the bitmap
        long dyp          -- the height of the bitmap
        bool fUpsideDown  -- indicates if the rows should be inverted

    Returns: fTrue if it could write the file
***************************************************************************/
bool FWriteBitmap(Filename *filename, byte *pixels, PDynamicArray palette, LegacyLong width, LegacyLong height,
                  bool upside)
{
    AssertPo(filename, ffniFile);
    AssertVarMem(pixels);
    AssertPo(palette, 0);
    Assert(palette->IvMac() == 256, "Invalid color palette");
    Assert(width >= 0, "Invalid width");
    Assert(height >= 0, "Invalid height");
#ifndef MAC
    static_assert(sizeof(Color) == 4, "BMP palette layout");
    Assert(palette->CbEntry() == sizeof(Color), "Palette has different format from Windows");
    BitmapContext context;
    auto s = BitmapServices(&context);
    return three_mm_bitmap_write(filename, pixels, palette, width, height, upside, &s) != 0;
#else
    RawRtn();
    return false;
#endif
}
