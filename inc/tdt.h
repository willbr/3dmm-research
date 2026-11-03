/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tdt.h: Three-D Text class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BaseCacheableObject ---> Template ---> ThreeDText  (Three-D Text)

***************************************************************************/
#ifndef ThreeDText_H
#define ThreeDText_H

// 3-D Text Shapes - the positions and orientations of the letters
enum
{
    tdtsNil = -1,
    tdtsNormal = 0,
    tdtsArchPositive,
    tdtsCircleY,
    tdtsLargeMiddle,
    tdtsArchNegative,
    tdtsArchZ,
    tdtsCircleZ,
    tdtsVertical,
    tdtsGrowRight,
    tdtsGrowLeft,
    tdtsLim
};

// 3-D Actions
enum
{
    tdaNil = -1,
    tdaRest = 0,
    tdaLetterRotX, // each letter rotates around its own X axis
    tdaLetterRotY,
    tdaLetterRotZ,
    tdaSwingX, // letters skew right, then left, then back to normal
    tdaSwingY,
    tdaSwingZ,
    tdaPulse,    // letters grow 10%, then shrink back to normal
    tdaWordRotX, // (rotate entire word around X axis)
    tdaWordRotY,
    tdaWordRotZ,
    tdaWave,    // a bump ripples through the letters
    tdaReveal,  // letters slowly grow from zero height
    tdaWalk,    // walk (word hops forward as if walking)
    tdaHop,     // letters hop up and down
    tdaStretch, // stretch in X
    tdaLim
};

/****************************************
    3-D Text class
****************************************/
typedef class ThreeDText *PThreeDText;
#define ThreeDText_PAR Template
#define kclsThreeDText 'TDT'
class ThreeDText : public ThreeDText_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    static PStringTable_GST _pgstAction; // Action names

    LegacyLong _tdts;          // ThreeDText shape
    TAG _tagTdf;         // Tag to Three-D Font
    PMaterial_MTRL _pmtrlDefault; // Material_MTRL for ThreeDText's default costume
    PActionDefinition _pactnCache;   // Last-used action
    LegacyLong _tdaCache;      // Action in pactnCache
    void *_fontContext = pvNil;
    PThreeDFont (*_fontResolver)(void *, const TAG &) = pvNil;
    PThreeDFont _FetchFont(void);

  protected:
    virtual bool _FInit(PChunkyFile pcfl, ChunkTagOrType ctgTmpl, ChunkNumber cnoTmpl);
    bool _FInitLists(void);
    PDynamicArray _PglibactParBuild(void);
    PDynamicArray _PglibsetBuild(void);
    PGeneralGroup _PggcmidBuild(void);
    PDynamicArray _Pglbmat34Build(LegacyLong tda);
    PGeneralGroup _PggcelBuild(LegacyLong tda);
    virtual PActionDefinition _PactnFetch(LegacyLong tda);
    PActionDefinition _PactnBuild(LegacyLong tda);
    virtual PModel _PmodlFetch(ChildChunkID chidModl);
    LegacyLong _CcelOfTda(LegacyLong tda);
    void _ApplyAction(BMAT34 *pbmat34, LegacyLong tda, LegacyLong ich, LegacyLong ccel, LegacyLong icel, BRS xrChar, BRS pdxrText);
    void _ApplyShape(BMAT34 *pbmat34, LegacyLong tdts, LegacyLong cch, LegacyLong ich, BRS xrChar, BRS dxrText, BRS yrChar, BRS dyrMax,
                     BRS dyrTotal);

  public:
    // Optional content service for headless hosts. Returns an owned reference;
    // the callback context must outlive this template and its cached actions.
    void SetFontResolver(void *context, PThreeDFont (*resolver)(void *, const TAG &))
    {
        _fontContext = context;
        _fontResolver = resolver;
    }
    static bool FSetActionNames(PStringTable_GST pgstAction);
#ifdef DEBUG
    static void MarkActionNames(void);
#endif

    static PThreeDText PtdtNew(PString pstn, LegacyLong tdts, PTAG ptagTdf);
    ~ThreeDText(void);
    static PDynamicArray PgltagFetch(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool *pfError);
    PThreeDText PtdtDup(void);

    void GetInfo(PString pstn, LegacyLong *ptdts, PTAG ptagTdf);
    bool FChange(PString pstn, LegacyLong tdts = tdtsNil, PTAG ptagTdf = pvNil);
    bool FWrite(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber *pcno);
    bool FAdjustBody(PBody pbody);
    virtual bool FSetDefaultCost(PBody pbody);
    virtual PCustomMaterial_CMTL PcmtlFetch(LegacyLong cmid);
    virtual bool FGetActnName(LegacyLong anid, PString pstn);
};

#endif // ThreeDText_H
