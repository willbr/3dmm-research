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
    long _cbRgcb; // size of the rgcb portion of _hqrgb
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
        long cb; // length of whole chunk, including the header
    };

    MaskedBitmapMBMP(void)
    {
    }
    virtual bool _FInit(byte *prgbPixels, long cbRow, long dyp, RC *prc, long xpRef, long ypRef, byte bTransparent,
                        ulong grfmbmp = fmbmpNil, byte bDefault = 0);

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

    static PMaskedBitmapMBMP PmbmpNew(byte *prgbPixels, long cbRow, long dyp, RC *prc, long xpRef, long ypRef, byte bTransparent,
                          ulong grfmbmp = fmbmpNil, byte bDefault = 0);
    static PMaskedBitmapMBMP PmbmpReadNative(Filename *pfni, byte bTransparent = 0, long xp = 0, long yp = 0, ulong grfmbmp = fmbmpNil,
                                 byte bDefault = 0);

    static PMaskedBitmapMBMP PmbmpRead(PDataBlock pblck);

    void GetRc(RC *prc);
    void Draw(byte *prgbPixels, long cbRow, long dyp, long xpRef, long ypRef, RC *prcClip = pvNil,
              PRegion pregnClip = pvNil);
    void DrawMask(byte *prgbPixels, long cbRow, long dyp, long xpRef, long ypRef, RC *prcClip = pvNil);
    bool FPtIn(long xp, long yp);

    virtual bool FWrite(PDataBlock pblck);
    virtual long CbOnFile(void);

    // a chunky resource reader for an MaskedBitmapMBMP
    static bool FReadMbmp(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb);
};
const ByteOrderMask kbomMbmph = 0xAFFC0000;

// reads a bitmap from the given file
bool FReadBitmap(Filename *pfni, byte **pprgb, PDynamicArray *ppglclr, long *pdxp, long *pdyp, bool *pfUpsideDown,
                 byte bTransparent = 0);

// writes a bitmap file
bool FWriteBitmap(Filename *pfni, byte *prgb, PDynamicArray pglclr, long dxp, long dyp, bool fUpsideDown = fTrue);

#endif //! MBMP_H
