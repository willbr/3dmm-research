/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    GFX classes: graphics port (GraphicsPort), graphics environment (GraphicsEnvironment)

***************************************************************************/
#ifndef GFX_H
#define GFX_H
#include "kauai_core.h"
#include "region.h"
#include "color.h"
class GraphicsPort;
class GraphicsObject;
class GraphicsEnvironment;
class Picture;
class MaskedBitmapMBMP;
typedef GraphicsPort *PGraphicsPort;
typedef GraphicsObject *PGraphicsObject;
typedef GraphicsEnvironment *PGraphicsEnvironment;
typedef Picture *PPicture;
typedef MaskedBitmapMBMP *PMaskedBitmapMBMP;
#ifdef KAUAI_SDL3
namespace KauaiPixels
{
struct Brush;
struct Brush8;
struct DibFrame;
class PixelBuffer;
struct MonochromeBrushState;
} // namespace KauaiPixels
namespace KauaiFonts
{
class Face;
struct Placement;
struct Mask;
} // namespace KauaiFonts
#endif

using Group::DynamicArray;
using Group::PDynamicArray;

/****************************************
    Text and fonts.
****************************************/
// DeScription of a Font.
struct FontDescription
{
    LegacyLong onn;     // Font number.
    LegacyUlong grfont; // Font style.
    LegacyLong dyp;     // Font height in points.
    LegacyLong tah;     // Horizontal Text Alignment
    LegacyLong tav;     // Vertical Text Alignment

    ASSERT
};

// fONT Styles - note that these match the Mac values
enum
{
    fontNil = 0,
    fontBold = 1,
    fontItalic = 2,
    fontUnderline = 4,
    fontBoxed = 8,
};

// Horizontal Text Alignment.
enum
{
    tahLeft,
    tahCenter,
    tahRight,
    tahLim
};

// Vertical Text Alignment
enum
{
    tavTop,
    tavCenter,
    tavBaseline,
    tavBottom,
    tavLim
};

/****************************************
    Font List
****************************************/
const long onnNil = -1;


#define FontList_PAR BASE
#define kclsFontList 'NTL'
class FontList : public FontList_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(FontList)

  private:
    PStringTable_GST _pgst;
    long _onnSystem;
#ifdef KAUAI_SDL3
    bool _fPortableFonts;
    bool _FInitPortable(void);
#endif

  public:
    FontList(void);
    ~FontList(void);

#ifdef WIN
    CoreNative::FontHandle HfntCreate(FontDescription *pdsf);
#endif // WIN
#ifdef MAC
    short FtcFromOnn(long onn);
#endif // MAC

    bool FInit(void);
    long OnnSystem(void)
    {
        return _onnSystem;
    }
    void GetStn(long onn, PString pstn);
    bool FGetOnn(PString pstn, long *ponn);
    long OnnMapStn(PString pstn, short osk = koskCur);
    long OnnMac(void);
    bool FFixedPitch(long onn);

#ifdef DEBUG
    bool FValidOnn(long onn);
#endif // DEBUG
};
extern FontList vntl;

/****************************************
    Color and pattern
****************************************/
#ifdef WIN
typedef LegacyUlong SCR;
#elif defined(MAC)
typedef RGBColor SCR;
#endif //! MAC

#include "color.h"

#ifdef DEBUG
enum
{
    facrNil,
    facrRgb = 1,
    facrIndex = 2,
};
#endif // DEBUG

enum
{
    kbNilAcr = 0,
    kbRgbAcr = 1,
    kbIndexAcr = 0xFE,
    kbSpecialAcr = 0xFF
};

const ulong kluAcrInvert = 0xFF000000L;
const ulong kluAcrClear = 0xFFFFFFFFL;

// Abstract ColoR
class AbstractColor
{
    friend class GraphicsPort;
    ASSERT

  private:
    LegacyUlong _lu;

#ifdef WIN
    SCR _Scr(void);
#endif // WIN
#ifdef MAC
    void _SetFore(void);
    void _SetBack(void);
#endif // MAC

  public:
    AbstractColor(void)
    {
        _lu = 0;
    }
    AbstractColor(Color clr)
    {
        _lu = LwFromBytes(kbRgbAcr, clr.bRed, clr.bGreen, clr.bBlue);
    }
    void Set(Color clr)
    {
        _lu = LwFromBytes(kbRgbAcr, clr.bRed, clr.bGreen, clr.bBlue);
    }
    AbstractColor(byte bRed, byte bGreen, byte bBlue)
    {
        _lu = LwFromBytes(kbRgbAcr, bRed, bGreen, bBlue);
    }
    void Set(byte bRed, byte bGreen, byte bBlue)
    {
        _lu = LwFromBytes(kbRgbAcr, bRed, bGreen, bBlue);
    }
    AbstractColor(byte iscr)
    {
        _lu = LwFromBytes(kbIndexAcr, 0, 0, iscr);
    }
    void SetToIndex(byte iscr)
    {
        _lu = LwFromBytes(kbIndexAcr, 0, 0, iscr);
    }
    AbstractColor(bool fClear, bool fIgnored)
    {
        _lu = fClear ? kluAcrClear : kluAcrInvert;
    }
    void SetToClear(void)
    {
        _lu = kluAcrClear;
    }
    void SetToInvert(void)
    {
        _lu = kluAcrInvert;
    }

    void SetFromLw(long lw);
    long LwGet(void) const;
    void GetClr(Color *pclr);

    bool operator==(const AbstractColor &acr) const
    {
        return _lu == acr._lu;
    }
    bool operator!=(const AbstractColor &acr) const
    {
        return _lu != acr._lu;
    }
};

#ifdef SYMC
extern AbstractColor kacrBlack;
extern AbstractColor kacrDkGray;
extern AbstractColor kacrGray;
extern AbstractColor kacrLtGray;
extern AbstractColor kacrWhite;
extern AbstractColor kacrRed;
extern AbstractColor kacrGreen;
extern AbstractColor kacrBlue;
extern AbstractColor kacrYellow;
extern AbstractColor kacrCyan;
extern AbstractColor kacrMagenta;
extern AbstractColor kacrClear;
extern AbstractColor kacrInvert;
#else  //! SYMC
const AbstractColor kacrBlack(0, 0, 0);
const AbstractColor kacrDkGray(0x3F, 0x3F, 0x3F);
const AbstractColor kacrGray(0x7F, 0x7F, 0x7F);
const AbstractColor kacrLtGray(0xBF, 0xBF, 0xBF);
const AbstractColor kacrWhite(kbMax, kbMax, kbMax);
const AbstractColor kacrRed(kbMax, 0, 0);
const AbstractColor kacrGreen(0, kbMax, 0);
const AbstractColor kacrBlue(0, 0, kbMax);
const AbstractColor kacrYellow(kbMax, kbMax, 0);
const AbstractColor kacrCyan(0, kbMax, kbMax);
const AbstractColor kacrMagenta(kbMax, 0, kbMax);
const AbstractColor kacrClear(fTrue, fTrue);
const AbstractColor kacrInvert(fFalse, fFalse);
#endif //! SYMC

// abstract pattern
struct AbstractPattern
{
    byte rgb[8];

    bool operator==(AbstractPattern &apt)
    {
        for (int i = 0; i < 8; ++i)
            if (rgb[i] != apt.rgb[i])
                return false;
        return true;
    }
    bool operator!=(AbstractPattern &apt)
    {
        return !(*this == apt);
    }

    void SetSolidFore(void)
    {
        for (auto &value : rgb)
            value = 0xFF;
    }
    bool FSolidFore(void)
    {
        for (auto value : rgb)
            if (value != 0xFF)
                return false;
        return true;
    }
    void SetSolidBack(void)
    {
        for (auto &value : rgb)
            value = 0;
    }
    bool FSolidBack(void)
    {
        for (auto value : rgb)
            if (value)
                return false;
        return true;
    }
    void Invert(void)
    {
        for (auto &value : rgb)
            value ^= 0xFF;
    }
    void MoveOrigin(long dxp, long dyp);
};
extern AbstractPattern vaptGray;
extern AbstractPattern vaptLtGray;
extern AbstractPattern vaptDkGray;

// The native OLY record is private to gfx_port.h.
/****************************************
    High level polygon - a DynamicArray of PT's.
****************************************/
enum
{
    fognNil = 0,
    fognAutoClose = 1,
    fognLim
};

typedef class Polygon *PPolygon;
#define Polygon_PAR DynamicArray
#define kclsPolygon 'OGN'
class Polygon : public Polygon_PAR
{
    RTCLASS_DEC

  private:
    struct AddEdgeInfo // Add Edge Info.
    {
        PT *prgpt;
        long cpt;
        long iptPenCur;
        PT ptCur;
        PPolygon pogn;
        long ipt;
        long dipt;
    };
    bool _FAddEdge(AddEdgeInfo *paei);

  protected:
    Polygon(void);

  public:
    PT *PrgptLock(long ipt = 0)
    {
        return (PT *)PvLock(ipt);
    }
    PT *QrgptGet(long ipt = 0)
    {
        return (PT *)QvGet(ipt);
    }

    PPolygon PognTraceOgn(PPolygon pogn, ulong grfogn);
    PPolygon PognTraceRgpt(PT *prgpt, long cpt, ulong grfogn);

    // static methods
    static PPolygon PognNew(long cvInit = 0);
};

long IptFindLeftmost(PT *prgpt, long cpt, long dxp, long dyp);

/****************************************
    Graphics drawing data - a parameter
    to drawing apis in the GraphicsPort class
****************************************/
enum
{
    fgddNil = 0,
    fgddFill = fgddNil,
    fgddFrame = 1,
    fgddPattern = 2,
    fgddAutoClose = 4,
};

// graphics drawing data
struct GraphicsDrawingData
{
    LegacyUlong grfgdd;    // what to do
    AbstractPattern apt;   // pattern to use
    AbstractColor acrFore; // foreground color (used for solid fills also)
    AbstractColor acrBack; // background color
    LegacyLong dxpPen;     // pen width (used if framing)
    LegacyLong dypPen;     // pen height
#ifndef MAC
    RC *prcsClip; // Portable clipping rectangle (may be pvNil).
#else
    SystemRectangle *prcsClip;
#endif
};

/****************************************
    Graphics environment
****************************************/
#define GraphicsEnvironment_PAR BASE
#define kclsGraphicsEnvironment 'GNV'
class GraphicsEnvironment : public GraphicsEnvironment_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    PGraphicsPort _pgpt; // the port

    // coordinate mapping
    RC _rcSrc;
    RC _rcDst;

    // current pen location and clipping
    long _xp;
    long _yp;
#ifndef MAC
    RC _rcsClip;
#else
    SystemRectangle _rcsClip;
#endif
    RC _rcVis; // always clipped to - this is in Dst coordinates

    // Current font
    FontDescription _dsf;

    // contains the current pen size and prcsClip
    // this is passed to the GraphicsPort
    GraphicsDrawingData _gdd;

    void _Init(PGraphicsPort pgpt);
    bool _FMapRcRcs(RC *prc, SystemRectangle *prcs);
    void _MapPtPts(long xp, long yp, SystemPoint *ppts);
    HQ _HqolyCreate(PPolygon pogn, ulong grfogn);
    HQ _HqolyFrame(PPolygon pogn, ulong grfogn);

    // transition related methods
    bool _FInitPaletteTrans(PDynamicArray pglclr, PDynamicArray *ppglclrOld, PDynamicArray *ppglclrTrans, long cbitPixel = 0);
    void _PaletteTrans(PDynamicArray pglclrOld, PDynamicArray pglclrNew, long lwNum, long lwDen, PDynamicArray pglclrTrans, Color *pclrSub = pvNil);
    bool _FEnsureTempGnv(PGraphicsEnvironment *ppgnv, RC *prc);

  public:
    GraphicsEnvironment(PGraphicsPort pgpt);
    GraphicsEnvironment(PGraphicsObject pgob);
    GraphicsEnvironment(PGraphicsObject pgob, PGraphicsPort pgpt);
    ~GraphicsEnvironment(void);

    void SetGobRc(PGraphicsObject pgob);
    PGraphicsPort Pgpt(void)
    {
        return _pgpt;
    }
#ifdef KAUAI_SDL3
    bool DrawVideoFrame(const KauaiPixels::DibFrame &, RC *);
#endif
#ifdef MAC
    void Set(void);
    void Restore(void);
#endif // MAC
#ifdef WIN
    // this gross API is for AVI playback
    void DrawDib(CoreNative::DibDrawHandle hdd, CoreNative::BitmapInfo *pbi, RC *prc);
#endif // WIN

    void SetPenSize(long dxp, long dyp);

    void FillRcApt(RC *prc, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack);
    void FillRc(RC *prc, AbstractColor acr);
    void FrameRcApt(RC *prc, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack);
    void FrameRc(RC *prc, AbstractColor acr);
    void HiliteRc(RC *prc, AbstractColor acrBack);

    void FillOvalApt(RC *prc, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack);
    void FillOval(RC *prc, AbstractColor acr);
    void FrameOvalApt(RC *prc, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack);
    void FrameOval(RC *prc, AbstractColor acr);

    void FillOgnApt(PPolygon pogn, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack);
    void FillOgn(PPolygon pogn, AbstractColor acr);
    void FrameOgnApt(PPolygon pogn, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack);
    void FrameOgn(PPolygon pogn, AbstractColor acr);
    void FramePolyLineApt(PPolygon pogn, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack);
    void FramePolyLine(PPolygon pogn, AbstractColor acr);

    void MoveTo(long xp, long yp)
    {
        _xp = xp;
        _yp = yp;
    }
    void MoveRel(long dxp, long dyp)
    {
        _xp += dxp;
        _yp += dyp;
    }
    void LineToApt(long xp, long yp, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack)
    {
        LineApt(_xp, _yp, xp, yp, papt, acrFore, acrBack);
    }
    void LineTo(long xp, long yp, AbstractColor acr)
    {
        Line(_xp, _yp, xp, yp, acr);
    }
    void LineRelApt(long dxp, long dyp, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack)
    {
        LineApt(_xp, _yp, _xp + dxp, _yp + dyp, papt, acrFore, acrBack);
    }
    void LineRel(long dxp, long dyp, AbstractColor acr)
    {
        Line(_xp, _yp, _xp + dxp, _yp + dyp, acr);
    }
    void LineApt(long xp1, long yp1, long xp2, long yp2, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack);
    void Line(long xp1, long yp1, long xp2, long yp2, AbstractColor acr);

    void ScrollRc(RC *prc, long dxp, long dyp, RC *prc1 = pvNil, RC *prc2 = pvNil);
    static void GetBadRcForScroll(RC *prc, long dxp, long dyp, RC *prc1, RC *prc2);

    // for mapping
    void GetRcSrc(RC *prc);
    void SetRcSrc(RC *prc);
    void GetRcDst(RC *prc);
    void SetRcDst(RC *prc);
    void SetRcVis(RC *prc);
    void IntersectRcVis(RC *prc);

    // set clipping
    void ClipRc(RC *prc);
    void ClipToSrc(void);

    // Text & font.
    void SetFont(long onn, ulong grfont, long dypFont, long tah = tahLeft, long tav = tavTop);
    void SetOnn(long onn);
    void SetFontStyle(ulong grfont);
    void SetFontSize(long dyp);
    void SetFontAlign(long tah, long tav);
    void GetDsf(FontDescription *pdsf);
    void SetDsf(FontDescription *pdsf);
    void DrawRgch(achar *prgch, long cch, long xp, long yp, AbstractColor acrFore = kacrBlack, AbstractColor acrBack = kacrClear);
    void DrawStn(PString pstn, long xp, long yp, AbstractColor acrFore = kacrBlack, AbstractColor acrBack = kacrClear);
    void GetRcFromRgch(RC *prc, achar *prgch, long cch, long xp = 0, long yp = 0);
    void GetRcFromStn(RC *prc, PString pstn, long xp = 0, long yp = 0);

    // bitmaps and pictures
    void CopyPixels(PGraphicsEnvironment pgnvSrc, RC *prcSrc, RC *prcDst);
    void DrawPic(PPicture ppic, RC *prc);
    void DrawMbmp(PMaskedBitmapMBMP pmbmp, long xp, long yp);
    void DrawMbmp(PMaskedBitmapMBMP pmbmp, RC *prc);

    // transitions
    void Wipe(long gfd, AbstractColor acrFill, PGraphicsEnvironment pgnvSrc, RC *prcSrc, RC *prcDst, ulong dts, PDynamicArray pglclr = pvNil);
    void Slide(long gfd, AbstractColor acrFill, PGraphicsEnvironment pgnvSrc, RC *prcSrc, RC *prcDst, ulong dts, PDynamicArray pglclr = pvNil);
    void Dissolve(long crcWidth, long crcHeight, AbstractColor acrFill, PGraphicsEnvironment pgnvSrc, RC *prcSrc, RC *prcDst, ulong dts,
                  PDynamicArray pglclr = pvNil);
    void Fade(long cactMax, AbstractColor acrFade, PGraphicsEnvironment pgnvSrc, RC *prcSrc, RC *prcDst, ulong dts, PDynamicArray pglclr = pvNil);
    void Iris(long gfd, long xp, long yp, AbstractColor acrFill, PGraphicsEnvironment pgnvSrc, RC *prcSrc, RC *prcDst, ulong dts,
              PDynamicArray pglclr = pvNil);
};

// palette setting options
enum
{
    fpalNil = 0,
    fpalIdentity = 1, // make this an identity palette
    fpalInitAnim = 2, // make the palette animatable
    fpalAnimate = 4,  // animate the current palette with these colors
};

// Native adapters include gfx_port.h for the full GraphicsPort definition.
/****************************************
    Misc.
****************************************/
bool FInitGfx(void);

#include "pixel_stretch.h"

// Number of times that the palette has changed (via a call to CclrSetPalette
// or SetActiveColors). This can be used by other modules to detect a palette
// change.
extern long vcactRealize;

#endif //! GFX_H
