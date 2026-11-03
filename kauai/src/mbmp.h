/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Masked Bitmap management code declarations.

***************************************************************************/
#ifndef MBMP_H
#define MBMP_H
#include "color.h"
class Region;
typedef Region *PRegion;
namespace KauaiPixels { struct Surface32; struct PackedSurface; struct Surface8; struct RleBitmap8; struct MaskedTransfer8; }

const FileType kftgBmp = 'BMP';

enum
{
    fmbmpNil = 0,
    fmbmpUpsideDown = 1,
    fmbmpMask = 2,
};

typedef class MaskedBitmapMBMP *PMaskedBitmapMBMP;
#define MaskedBitmapMBMP_PAR BaseCacheableObject
#define kclsMaskedBitmapMBMP 'MBMP'
class MaskedBitmapMBMP : public MaskedBitmapMBMP_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // rc (in the MaskedBitmapOnFile) is the bounding rectangle of the mbmp. It implicitly
    // holds the reference point.

    // _hqrgb holds an MaskedBitmapOnFile followed by an array of the length of each row
    // (rgcb) followed by the actual pixel data. The rgcb is an array of shorts
    // of length rc.Dyp(). We store the whole MaskedBitmapOnFile in the _hqrgb so that
    // loading the MaskedBitmapMBMP from a chunky file is fast. If the chunk is compressed,
    // storing anything less than the full chunk in _hqrgb requires another blt.

    // The pixel data is stored row by row with transparency encoded using
    // an RLE scheme. For each row, the first byte is the number of consecutive
    // tranparent pixels. The next byte is the number of consecutive
    // non-transparent pixels (cb). The next cb bytes are the values of
    // the non-transparent pixels. This order repeats itself for the rest of
    // the row, and then the next row begins. Rows should never end with a
    // transparent byte.

    // If fMask is true, the non-transparent pixels are not in _hqrgb. Instead,
    // all non-transparent pixels have the value bFill.
    LegacyLong _cbRgcb; // size of the rgcb portion of _hqrgb
    HQ _hqrgb;    // MaskedBitmapOnFile, short rgcb[_rc.Dyp()] followed by the pixel data

    // MaskedBitmapMBMP header on file
    struct MaskedBitmapOnFile
    {
        short bo;
        short osk;
        byte fMask;
        byte bFill;       // if fMask, the color value to use
        short swReserved; // should be zero on file
        RC rc;
        LegacyLong cb; // length of whole chunk, including the header
    };

    MaskedBitmapMBMP(void)
    {
    }
    virtual bool _FInit(byte *prgbPixels, LegacyLong cbRow, LegacyLong dyp, RC *prc, LegacyLong xpRef, LegacyLong ypRef, byte bTransparent,
                        LegacyUlong grfmbmp = fmbmpNil, byte bDefault = 0);
    bool _FGetRleBitmap(KauaiPixels::RleBitmap8 *bitmap);

    short *_Qrgcb(void)
    {
        return (short *)PvAddBv(QvFromHq(_hqrgb), size(MaskedBitmapOnFile));
    }
    MaskedBitmapOnFile *_Qmbmph(void)
    {
        return (MaskedBitmapOnFile *)QvFromHq(_hqrgb);
    }

  public:
    ~MaskedBitmapMBMP(void);

    static PMaskedBitmapMBMP PmbmpNew(byte *prgbPixels, LegacyLong cbRow, LegacyLong dyp, RC *prc, LegacyLong xpRef, LegacyLong ypRef, byte bTransparent,
                          LegacyUlong grfmbmp = fmbmpNil, byte bDefault = 0);
    static PMaskedBitmapMBMP PmbmpReadNative(Filename *pfni, byte bTransparent = 0, LegacyLong xp = 0, LegacyLong yp = 0, LegacyUlong grfmbmp = fmbmpNil,
                                 byte bDefault = 0);

    static PMaskedBitmapMBMP PmbmpRead(PDataBlock pblck);

    void GetRc(RC *prc);
    void Draw(byte *prgbPixels, LegacyLong cbRow, LegacyLong dyp, LegacyLong xpRef, LegacyLong ypRef, RC *prcClip = pvNil,
              PRegion pregnClip = pvNil);
    void DrawMask(byte *prgbPixels, LegacyLong cbRow, LegacyLong dyp, LegacyLong xpRef, LegacyLong ypRef, RC *prcClip = pvNil);
    bool FDrawPacked(const KauaiPixels::PackedSurface &destination, const RC &rectangle,
                     const Color *palette, LegacyLong colors, const RC *clip = pvNil,
                     PRegion region = pvNil, bool andShrink = false);
    bool FDraw32(const KauaiPixels::Surface32 &destination, const RC &rectangle,
                  const Color *palette, LegacyLong colors, const RC *clip = pvNil, PRegion region = pvNil);
    bool FDraw8(const KauaiPixels::Surface8 &destination, const RC &rectangle,
                 const RC *clip = pvNil, PRegion region = pvNil);
    bool FDraw8(const KauaiPixels::Surface8 &destination, const RC &rectangle,
                 const KauaiPixels::MaskedTransfer8 &transfer, const RC *clip = pvNil, PRegion region = pvNil,
                 bool andShrink = false);
    bool FPtIn(LegacyLong xp, LegacyLong yp);

    virtual bool FWrite(PDataBlock pblck);
    virtual LegacyLong CbOnFile(void);

    // a chunky resource reader for an MaskedBitmapMBMP
    static bool FReadMbmp(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb);
};
const ByteOrderMask kbomMbmph = 0xAFFC0000;

// reads a bitmap from the given file
bool FReadBitmap(Filename *pfni, byte **pprgb, PDynamicArray *ppglclr, LegacyLong *pdxp, LegacyLong *pdyp, bool *pfUpsideDown,
                 byte bTransparent = 0);

// writes a bitmap file
bool FWriteBitmap(Filename *pfni, byte *prgb, PDynamicArray pglclr, LegacyLong dxp, LegacyLong dyp, bool fUpsideDown = fTrue);

#endif //! MBMP_H
