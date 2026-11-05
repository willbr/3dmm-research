/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_GFX_PORT_H
#define KAUAI_GFX_PORT_H
// Native rasterizer bridge. High-level clients include gfx.h and keep the port
// opaque; runtime/native adapters include this definition through frame.h.
#include "win_headers.h"
#include "gfx.h"
struct OLY // pOLYgon
{
#ifdef MAC
    short cb;            // size of the whole thing
    SystemRectangle rcs; // bounding rectangle
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
void BuildLinePolygon(OLY *polygon, SystemPoint first, SystemPoint last, LegacyLong penX, LegacyLong penY);

#define GraphicsPort_PAR BASE
#define kclsGraphicsPort 'GPT'
class GraphicsPort : public GraphicsPort_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    PRegion _pregnClip;
    bool _fNewClip : 1; // Shared clipping state; invalidates the selected backend clip.
    RC _rcClip;
    PT _ptBase; // coordinates assigned to top-left of the GraphicsPort

#if defined(WIN) || defined(KAUAI_SDL3)
    static Color *_prgclr;
    static LegacyLong _cclrPal;
    byte *_prgbPixels;
    LegacyLong _cbitPixel, _cbRow;
    RC _rcOff;
    long _cactLock;
    void _EnsurePalette(void);
#endif
#ifdef KAUAI_SDL3
    KauaiFonts::Face *_fontFace;
    FontDescription _fontDescription;
    bool _fontPortable;
    bool _FPortableTextFace(FontDescription *font);
    bool _FEnsureTextFace(FontDescription *font);
    bool _FHostText(achar *text, long count, SystemPoint point, FontDescription *font, KauaiFonts::Placement &placement,
                    KauaiFonts::Mask *mask = nullptr, bool opaque = false);
    bool _FDrawPixelText(achar *text, long count, SystemPoint point, GraphicsDrawingData *drawing,
                         FontDescription *font);
    Color _rgclrPixels[256]; // Private palette for packed CPU storage.
    KauaiPixels::PixelBuffer *_pixelBuffer;
    bool _fPixelColorOnColor;
    std::uint64_t _pixelPaletteVersion = 0;
    bool _pixelOwnPalette = false;
    bool _monoBrushValid = false;
    AbstractColor _monoBrushColor;
    LegacyUlong _monoBrushBackground = 0x00FFFFFF;
    LegacyUlong _monoBackground = 0x00FFFFFF;
    // Adapter hooks retain the legacy packed BGR/reference color encoding.
    std::uint32_t _PixelBackgroundColor() const;
    byte _PixelColorReference(AbstractColor color);
    bool _FDrawPixelRect(SystemRectangle *rectangle, GraphicsDrawingData *drawing, bool hilite = false);
    bool _FDrawPixelOval(SystemRectangle *rectangle, GraphicsDrawingData *drawing);
    bool _FGetPixelBrush(GraphicsDrawingData *drawing, KauaiPixels::Brush &brush, bool hilite = false);
    KauaiPixels::MonochromeBrushState _MonochromeBrushState(GraphicsDrawingData *drawing, bool selectBrush = false);
    bool _FGetPixelBrush8(GraphicsDrawingData *drawing, KauaiPixels::Brush8 &brush, bool hilite = false);
    bool _FDrawPixelPolygon(const OLY *polygon, GraphicsDrawingData *drawing);
    bool _FDrawPackedCopy(PGraphicsPort source, SystemRectangle *sourceRect, SystemRectangle *destinationRect,
                          GraphicsDrawingData *drawing);
    bool _FCopyPixels(PGraphicsPort source, SystemRectangle *sourceRect, SystemRectangle *destinationRect,
                      GraphicsDrawingData *drawing);
    bool _FScrollPixels(SystemRectangle *rectangle, long dx, long dy, GraphicsDrawingData *drawing);
    bool _FDrawPixelMbmp(PMaskedBitmapMBMP bitmap, SystemRectangle *rectangle, GraphicsDrawingData *drawing);
    bool _PixelOwnPalette() const;
    void _SetPixelBackgroundColor(std::uint32_t color);
    static long _cPortableIndexedPorts;
#endif

#ifdef WIN
#ifdef DEBUG
    static bool _fFlushGdi;
#endif
    static HPAL _hpal;
    static HPAL _hpalIdentity;
    static bool _FEnsureIdentityPalette();
    static long _cactPalCur;
    static long _cactFlush;
    static bool _fPalettized; // whether the screen is palettized

    HDC _hdc;
    HWND _hwnd;
    HBMP _hbmp;        // Native bitmap; SDL pixel ports can allocate it on demand.
#ifdef KAUAI_SDL3
    static bool _fIdentityPaletteRequested;
    static long _cIndexedPortsCreated, _cIndexedContextsCreated, _cIndexedMirrorsCreated;
    byte *_prgbNative;         // Temporary Windows drawing mirror; not pixel ownership.
    bool _fUseIdentityPalette; // Preserve the logical palette selection from port creation.
    bool _FEnsureNativeContext();
    bool _FEnsureNativePixels();
    bool _UploadNativePixels();
    void _FlushNativePixels();
#endif
    long _cactPal;  // which palette this port has selected
    long _cactDraw; // last draw - for knowing when to call GdiFlush

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
    AbstractPattern _apt; // for bkApt
    AbstractColor _acr;   // for bkAcr
    int _wType;           // for bkStock (stock brush)

    HFONT _hfnt;
#ifdef KAUAI_SDL3
    HFONT _fontSource;
#endif
    FontDescription _dsf;

    bool _fMetaFile : 1;
    bool _fMapIndices : 1; // SelectPalette failed, map indices to RGBs
    bool _fOwnPalette : 1; // this offscreen has its own palette

    bool _SetClip(RC *prcsClip);
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
#if defined(WIN) || defined(KAUAI_SDL3)
    static PGraphicsPort PgptNewHwnd(CoreNative::WindowHandle window);
#endif
#ifdef KAUAI_SDL3
    static PGraphicsPort PgptNewSDLWindow(CoreNative::WindowHandle window);
#ifndef WIN
    bool FHasNativeMirror() const { return false; }
    bool FHasNativeContext() const { return false; }
    static long CIndexedPortsCreated() { return _cPortableIndexedPorts; }
    static long CIndexedContextsCreated() { return 0; }
    static long CIndexedMirrorsCreated() { return 0; }
#endif
#endif
#ifdef WIN
    static PGraphicsPort PgptNew(HDC hdc);
#ifdef KAUAI_SDL3
    bool FHasNativeMirror() const
    {
        return _prgbNative != nullptr;
    }
    bool FHasNativeContext() const
    {
        return _hdc != nullptr;
    }
    static long CIndexedPortsCreated()
    {
        return _cIndexedPortsCreated;
    }
    static long CIndexedContextsCreated()
    {
        return _cIndexedContextsCreated;
    }
    static long CIndexedMirrorsCreated()
    {
        return _cIndexedMirrorsCreated;
    }
#endif

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
    PPicture PpicRelease(void);
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

    void CopyPixels(PGraphicsPort pgptSrc, SystemRectangle *prcsSrc, SystemRectangle *prcsDst,
                    GraphicsDrawingData *pgdd);
    void DrawPic(PPicture ppic, SystemRectangle *prcs, GraphicsDrawingData *pgdd);
    void DrawMbmp(PMaskedBitmapMBMP pmbmp, SystemRectangle *prcs, GraphicsDrawingData *pgdd);
#ifdef KAUAI_SDL3
    bool DrawVideoFrame(const KauaiPixels::DibFrame &, SystemRectangle *, GraphicsDrawingData *);
    bool FCopyIndexedSnapshot(PGraphicsPort source, SystemRectangle *sourceRect, SystemRectangle *destinationRect);
    bool FGetIndexedPalette(Color *colors);
#endif

    void Lock(void);
    void Unlock(void);
    byte *PrgbLockPixels(RC *prc = pvNil);
    long CbRow(void);
    long CbitPixel(void);
};

/****************************************
    Regions
****************************************/
#ifdef WIN
bool FCreateRgn(HRGN *phrgn, RC *prc);
void FreePhrgn(HRGN *phrgn);
bool FSetRectRgn(HRGN *phrgn, RC *prc);
bool FUnionRgn(HRGN hrgnDst, HRGN hrgnSrc1, HRGN hrgnSrc2);
bool FIntersectRgn(HRGN hrgnDst, HRGN hrgnSrc1, HRGN hrgnSrc2, bool *pfEmpty = pvNil);
bool FDiffRgn(HRGN hrgnDst, HRGN hrgnSrc, HRGN hrgnSrcSub, bool *pfEmpty = pvNil);
bool FRectRgn(HRGN hrgn, RC *prc = pvNil);
bool FEmptyRgn(HRGN hrgn, RC *prc = pvNil);
bool FEqualRgn(HRGN hrgn1, HRGN hrgn2);
#endif

#endif
