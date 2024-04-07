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

using Group::DynamicArray;
using Group::PDynamicArray;

/****************************************
    Text and fonts.
****************************************/
// DeScription of a Font.
struct FontDescription
{
    long onn;     // Font number.
    ulong grfont; // Font style.
    long dyp;     // Font height in points.
    long tah;     // Horizontal Text Alignment
    long tav;     // Vertical Text Alignment

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

#ifdef WIN
int CALLBACK _FEnumFont(LOGFONT *plgf, TEXTMETRIC *ptxm, ulong luType, LPARAM luParam);
#endif // WIN

#define FontList_PAR BASE
#define kclsFontList 'NTL'
class FontList : public FontList_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(FontList)

  private:
#ifdef WIN
    friend int CALLBACK _FEnumFont(LOGFONT *plgf, TEXTMETRIC *ptxm, ulong luType, LPARAM luParam);
#endif // WIN
    PStringTable _pgst;
    long _onnSystem;

  public:
    FontList(void);
    ~FontList(void);

#ifdef WIN
    HFONT HfntCreate(FontDescription *pdsf);
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
typedef COLORREF SCR;
#elif defined(MAC)
typedef RGBColor SCR;
#endif //! MAC

// NOTE: this matches the Windows RGBQUAD structure
struct Color
{
    byte bBlue;
    byte bGreen;
    byte bRed;
    byte bZero;
};

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
    ulong _lu;

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
        return ((long *)rgb)[0] == ((long *)apt.rgb)[0] && ((long *)rgb)[1] == ((long *)apt.rgb)[1];
    }
    bool operator!=(AbstractPattern &apt)
    {
        return ((long *)rgb)[0] != ((long *)apt.rgb)[0] || ((long *)rgb)[1] != ((long *)apt.rgb)[1];
    }

    void SetSolidFore(void)
    {
        ((long *)rgb)[0] = -1L;
        ((long *)rgb)[1] = -1L;
    }
    bool FSolidFore(void)
    {
        return (((long *)rgb)[0] & ((long *)rgb)[1]) == -1L;
    }
    void SetSolidBack(void)
    {
        ((long *)rgb)[0] = 0L;
        ((long *)rgb)[1] = 0L;
    }
    bool FSolidBack(void)
    {
        return (((long *)rgb)[0] | ((long *)rgb)[1]) == 0L;
    }
    void Invert(void)
    {
        ((long *)rgb)[0] = ~((long *)rgb)[0];
        ((long *)rgb)[1] = ~((long *)rgb)[1];
    }
    void MoveOrigin(long dxp, long dyp);
};
extern AbstractPattern vaptGray;
extern AbstractPattern vaptLtGray;
extern AbstractPattern vaptDkGray;

/****************************************
    Polygon structure - designed to be
    compatible with the Mac's
    Polygon.
****************************************/
struct OLY // pOLYgon
{
#ifdef MAC
    short cb; // size of the whole thing
    SystemRectangle rcs;  // bounding rectangle
    SystemPoint rgpts[1];

    long Cpts(void)
    {
        return (cb - offset(OLY, rgpts[0])) / size(SystemPoint);
    }
#else  //! MAC
    long cpts;
    SystemPoint rgpts[1];

    long Cpts(void)
    {
        return cpts;
    }
#endif //! MAC

    ASSERT
};
const long kcbOlyBase = size(OLY) - size(SystemPoint);

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
    ulong grfgdd;  // what to do
    AbstractPattern apt;       // pattern to use
    AbstractColor acrFore;   // foreground color (used for solid fills also)
    AbstractColor acrBack;   // background color
    long dxpPen;   // pen width (used if framing)
    long dypPen;   // pen height
    SystemRectangle *prcsClip; // clipping (may be pvNil)
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
    SystemRectangle _rcsClip;
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
#ifdef MAC
    void Set(void);
    void Restore(void);
#endif // MAC
#ifdef WIN
    // this gross API is for AVI playback
    void DrawDib(HDRAWDIB hdd, BITMAPINFOHEADER *pbi, RC *prc);
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
    void DrawPic(PPIC ppic, RC *prc);
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

/****************************************
    Graphics port
****************************************/
#define GraphicsPort_PAR BASE
#define kclsGraphicsPort 'GPT'
class GraphicsPort : public GraphicsPort_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    PRegion _pregnClip;
    RC _rcClip;
    PT _ptBase; // coordinates assigned to top-left of the GraphicsPort

#ifdef WIN
#ifdef DEBUG
    static bool _fFlushGdi;
#endif
    static HPAL _hpal;
    static HPAL _hpalIdentity;
    static Color *_prgclr;
    static long _cclrPal;
    static long _cactPalCur;
    static long _cactFlush;
    static bool _fPalettized; // whether the screen is palettized

    HDC _hdc;
    HWND _hwnd;
    HBMP _hbmp;        // nil if not an offscreen port
    byte *_prgbPixels; // nil if not a dib section port
    long _cbitPixel;
    long _cbRow;
    RC _rcOff;      // bounding rectangle for a metafile or dib port
    long _cactPal;  // which palette this port has selected
    long _cactDraw; // last draw - for knowing when to call GdiFlush
    long _cactLock; // lock count

    // selected brush and its related info
    enum // brush kind
    {
        bkNil,
        bkApt,
        bkAcr,
        bkStock
    };
    HBRUSH _hbr;
    long _bk;
    AbstractPattern _apt;   // for bkApt
    AbstractColor _acr;   // for bkAcr
    int _wType; // for bkStock (stock brush)

    HFONT _hfnt;
    FontDescription _dsf;

    bool _fNewClip : 1; // _pregnClip has changed
    bool _fMetaFile : 1;
    bool _fMapIndices : 1; // SelectPalette failed, map indices to RGBs
    bool _fOwnPalette : 1; // this offscreen has its own palette

    void _SetClip(SystemRectangle *prcsClip);
    void _EnsurePalette(void);
    void _SetTextProps(FontDescription *pdsf);
    void _SetAptBrush(AbstractPattern *papt);
    void _SetAcrBrush(AbstractColor acr);
    void _SetStockBrush(int wType);

    void _FillRcs(SystemRectangle *prcs);
    void _FillOval(SystemRectangle *prcs);
    void _FillPoly(OLY *poly);
    void _FillRgn(HRGN *phrgn);
    void _FrameRcsOval(SystemRectangle *prcs, GraphicsDrawingData *pgdd, bool fOval);
    SCR _Scr(AbstractColor acr);

    bool _FInit(HDC hdc);
#endif // WIN

#ifdef MAC
    static HCLT _hcltDef;
    static bool _fForcePalOnSys;
    static HCLT _HcltUse(long cbitPixel);

    // WARNING: the PPRT's below may be GWorldPtr's instead of GrafPtr's
    // Only use SetGWorld or GetGWorld on these.  Don't assume they
    // point to GrafPort's.
    PPRT _pprt; // may be a GWorldPtr
    HGD _hgd;
    PPRT _pprtSav; // may be a GWorldPtr
    HGD _hgdSav;
    short _cactLock;  // lock count for pixels (if offscreen)
    short _cbitPixel; // depth of bitmap (if offscreen)
    bool _fSet : 1;
    bool _fOffscreen : 1;
    bool _fNoClip : 1;
    bool _fNewClip : 1; //_pregnClip is new

    // for picture based GraphicsPort's
    RC _rcOff; // also valid for offscreen GPTs
    HPIC _hpic;

    HPIX _Hpix(void);
    void _FillRcs(SystemRectangle *prcs);
    void _FrameRcs(SystemRectangle *prcs);
    void _FillOval(SystemRectangle *prcs);
    void _FrameOval(SystemRectangle *prcs);
    void _FillPoly(HQ *phqoly);
    void _FramePoly(HQ *phqoly);
    void _DrawLine(SystemPoint *prgpts);
    void _GetRcsFromRgch(SystemRectangle *prcs, achar *prgch, short cch, SystemPoint *ppts, FontDescription *pdsf);
#endif // MAC

    // low level draw routine
    typedef void (GraphicsPort::*PFNDRW)(void *);
    void _Fill(void *pv, GraphicsDrawingData *pgdd, PFNDRW pfn);

    GraphicsPort(void)
    {
    }
    ~GraphicsPort(void);

  public:
#ifdef WIN
    static PGraphicsPort PgptNew(HDC hdc);
    static PGraphicsPort PgptNewHwnd(HWND hwnd);

    static long CclrSetPalette(HWND hwnd, bool fInval);

    // this gross API is for AVI playback
    void DrawDib(HDRAWDIB hdd, BITMAPINFOHEADER *pbi, SystemRectangle *prcs, GraphicsDrawingData *pgdd);
#endif // WIN
#ifdef MAC
    static PGraphicsPort PgptNew(PPRT pprt, HGD hgd = hNil);

    static bool FCanScreen(long cbitPixel, bool fColor);
    static bool FSetScreenState(long cbitPixel, bool tColor);
    static void GetScreenState(long *pcbitPixel, bool *pfColor);

    void Set(SystemRectangle *prcsClip);
    void Restore(void);
#endif // MAC
#ifdef DEBUG
    static void MarkStaticMem(void);
#endif // DEBUG

    static void SetActiveColors(PDynamicArray pglclr, ulong grfpal);
    static PDynamicArray PglclrGetPalette(void);
    static void Flush(void);

    static PGraphicsPort PgptNewOffscreen(RC *prc, long cbitPixel);
    static PGraphicsPort PgptNewPic(RC *prc);
    PPIC PpicRelease(void);
    void SetOffscreenColors(PDynamicArray pglclr = pvNil);

    void ClipToRegn(PRegion *ppregn);
    void SetPtBase(PT *ppt);
    void GetPtBase(PT *ppt);

    void DrawRcs(SystemRectangle *prcs, GraphicsDrawingData *pgdd);
    void HiliteRcs(SystemRectangle *prcs, GraphicsDrawingData *pgdd);
    void DrawOval(SystemRectangle *prcs, GraphicsDrawingData *pgdd);
    void DrawLine(SystemPoint *ppts1, SystemPoint *ppts2, GraphicsDrawingData *pgdd);
    void DrawPoly(HQ hqoly, GraphicsDrawingData *pgdd);
    void ScrollRcs(SystemRectangle *prcs, long dxp, long dyp, GraphicsDrawingData *pgdd);

    void DrawRgch(achar *prgch, long cch, SystemPoint pts, GraphicsDrawingData *pgdd, FontDescription *pdsf);
    void GetRcsFromRgch(SystemRectangle *prcs, achar *prgch, long cch, SystemPoint pts, FontDescription *pdsf);

    void CopyPixels(PGraphicsPort pgptSrc, SystemRectangle *prcsSrc, SystemRectangle *prcsDst, GraphicsDrawingData *pgdd);
    void DrawPic(PPIC ppic, SystemRectangle *prcs, GraphicsDrawingData *pgdd);
    void DrawMbmp(PMaskedBitmapMBMP pmbmp, SystemRectangle *prcs, GraphicsDrawingData *pgdd);

    void Lock(void);
    void Unlock(void);
    byte *PrgbLockPixels(RC *prc = pvNil);
    long CbRow(void);
    long CbitPixel(void);
};

/****************************************
    Regions
****************************************/
bool FCreateRgn(HRGN *phrgn, RC *prc);
void FreePhrgn(HRGN *phrgn);
bool FSetRectRgn(HRGN *phrgn, RC *prc);
bool FUnionRgn(HRGN hrgnDst, HRGN hrgnSrc1, HRGN hrgnSrc2);
bool FIntersectRgn(HRGN hrgnDst, HRGN hrgnSrc1, HRGN hrgnSrc2, bool *pfEmpty = pvNil);
bool FDiffRgn(HRGN hrgnDst, HRGN hrgnSrc, HRGN hrgnSrcSub, bool *pfEmpty = pvNil);
bool FRectRgn(HRGN hrgn, RC *prc = pvNil);
bool FEmptyRgn(HRGN hrgn, RC *prc = pvNil);
bool FEqualRgn(HRGN hrgn1, HRGN hrgn2);

/****************************************
    Misc.
****************************************/
bool FInitGfx(void);

// stretch by a factor of 2 in each dimension.
void DoubleStretch(byte *prgbSrc, long cbRowSrc, long dypSrc, RC *prcSrc, byte *prgbDst, long cbRowDst, long dypDst,
                   long xpDst, long ypDst, RC *prcClip, PRegion pregnClip);

// stretch by a factor of 2 in vertical direction only.
void DoubleVertStretch(byte *prgbSrc, long cbRowSrc, long dypSrc, RC *prcSrc, byte *prgbDst, long cbRowDst, long dypDst,
                       long xpDst, long ypDst, RC *prcClip, PRegion pregnClip);

// Number of times that the palette has changed (via a call to CclrSetPalette
// or SetActiveColors). This can be used by other modules to detect a palette
// change.
extern long vcactRealize;

#endif //! GFX_H
