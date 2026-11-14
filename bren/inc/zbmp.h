/*************************************************************************

    zbmp.h: Z-buffer Bitmap Class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BaseCacheableObject ---> ZBMP

*************************************************************************/
#ifndef ZBMP_H
#define ZBMP_H
#include "three_mm/depth_bitmap.h"

namespace BRender {

#define kcbPixelZbmp 2 // Z-buffers are 2 bytes per pixel (16 bit)

// ZBMP on file
struct ZBMPF
{
    short bo;
    short osk;
    short xpLeft;
    short ypTop;
    short dxp;
    short dyp;
    // void *rgb; 		// pixels follow immediately after ZBMPF
};
const br_uint_32 kbomZbmpf = 0x55500000;

/****************************************
    ZBMP class
****************************************/
typedef class ZBMP *PZBMP;
#define ZBMP_PAR BaseCacheableObject
#define kclsZBMP 'ZBMP'
class ZBMP : public ZBMP_PAR
{
    friend class World;
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    ThreeMMDepthBitmap _depth;
    static ThreeMMDepthServices _Services(void *context = nullptr);
    ZBMP(void)
    {
    }

  public:
    static PZBMP PzbmpNew(br_int_32 dxp, br_int_32 dyp);
    static PZBMP PzbmpNewFromBpmp(BPMP *pbpmp);
    static PZBMP PzbmpRead(PDataBlock pblck);
    static bool FReadZbmp(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, br_int_32 *pcb);
    ~ZBMP(void);

    byte *Prgb(void)
    {
        return static_cast<byte *>(_depth.pixels);
    }
    br_int_32 CbRow(void)
    {
        return _depth.row_bytes;
    }

    void Draw(byte *prgbPixels, br_int_32 cbRow, br_int_32 dyp, br_int_32 xpRef, br_int_32 ypRef, RC *prcClip = pvNil,
              PRegion pregnClip = pvNil);
    void DrawHalf(byte *prgbPixels, br_int_32 cbRow, br_int_32 dyp, br_int_32 xpRef, br_int_32 ypRef, RC *prcClip = pvNil,
                  PRegion pregnClip = pvNil);

    bool FWrite(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber *pcno);
};

} // end of namespace BRender

#endif ZBMP_H
