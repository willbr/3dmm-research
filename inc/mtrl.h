/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/*************************************************************************

    mtrl.h: Material and custom material classes

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BaseCacheableObject ---> Material_MTRL
    BASE ---> BaseCacheableObject ---> CMTL

*************************************************************************/
#ifndef MTRL_H
#define MTRL_H

using namespace BRender;

// CMTL on File
struct CMTLF
{
    short bo;
    short osk;
    long ibset; // which body part set this CMTL attaches to
};
const ByteOrderMask kbomCmtlf = 0x5c000000;

// material on file (Material_MTRL chunk)
struct MTRLF
{
    short bo;            // byte order
    short osk;           // OS kind
    br_colour brc;       // RGB color
    br_ufraction brufKa; // ambient component
    br_ufraction brufKd; // diffuse component
    br_ufraction brufKs; // specular component
    byte bIndexBase;     // base of palette for this color
    byte cIndexRange;    // count of entries in palette for this color
    BRS rPower;          // specular exponent
};
const ByteOrderMask kbomMtrlf = 0x5D530000;

/****************************************
    The Material_MTRL class.  There are two kinds
    of MTRLs: solid-color MTRLs and
    texmap materials.  Texmap MTRLs have
    TMAPs under the Material_MTRL chunk with chid
    0.
****************************************/
typedef class Material_MTRL *PMaterial_MTRL;
#define Material_MTRL_PAR BaseCacheableObject
#define kclsMaterial_MTRL 'MTRL'
class Material_MTRL : public Material_MTRL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    static PTextureMap _ptmapShadeTable; // shade table for all MTRLs
    PBMTL _pbmtl;

  protected:
    Material_MTRL(void)
    {
        _pbmtl = pvNil;
    } // can't instantiate directly; must use FReadMtrl
    bool _FInit(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno);

  public:
    static bool FSetShadeTable(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    static PMaterial_MTRL PmtrlNew(long iclrBase = ivNil, long cclr = ivNil);
    static bool FReadMtrl(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb);
    static PMaterial_MTRL PmtrlNewFromPix(PFilename pfni);
    static PMaterial_MTRL PmtrlNewFromBmp(PFilename pfni, PDynamicArray pglclr = pvNil);
    static PMaterial_MTRL PmtrlFromBmtl(PBMTL pbmtl);
    ~Material_MTRL(void);
    PTextureMap Ptmap(void);
    PBMTL Pbmtl(void)
    {
        return _pbmtl;
    }
    bool FWrite(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber *pcno);
#ifdef DEBUG
    static void MarkShadeTable(void);
#endif // DEBUG
};

/****************************************
    The CMTL (custom material) class
    This manages a set of materials to
    apply to a body part set
****************************************/
typedef class CMTL *PCMTL;
#define CMTL_PAR BaseCacheableObject
#define kclsCMTL 'CMTL'
class CMTL : public CMTL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PMaterial_MTRL *_prgpmtrl; // _cbprt PMTRLs, one per body part in this CMTL's set
    PMODL *_prgpmodl; // _cbprt PMODLs, one per body part in this CMTL's set
    long _cbprt;      // count of body parts in this CMTL
    long _ibset;      // body part set that this CMTL should be applied to

  protected:
    bool _FInit(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno);
    CMTL(void)
    {
    } // can't instantiate directly; must use PcmtlRead

  public:
    static PCMTL PcmtlNew(long ibset, long cbprt, PMaterial_MTRL *prgpmtrl);
    static bool FReadCmtl(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb);
    static bool FHasModels(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    static bool FEqualModels(PChunkyFile pcfl, ChunkNumber cno1, ChunkNumber cno2);
    ~CMTL(void);
    PBMTL Pbmtl(long ibmtl);
    PMODL Pmodl(long imodl);
    long Ibset(void)
    {
        return _ibset;
    }
    long Cbprt(void)
    {
        return _cbprt;
    }
    bool FHasModels(void);
};

#endif // !MTRL_H
