/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/*************************************************************************

    mtrl.h: Material and custom material classes

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BaseCacheableObject ---> Material_MTRL
    BASE ---> BaseCacheableObject ---> CustomMaterial_CMTL

*************************************************************************/
#ifndef MTRL_H
#define MTRL_H

#include <cstdint>
#include "three_mm/material_resource.h"

using namespace BRender;

// CustomMaterial_CMTL on File
struct CustomMaterialOnFile
{
    int16_t bo;
    int16_t osk;
    int32_t ibset; // which body part set this CustomMaterial_CMTL attaches to
};
static_assert(sizeof(CustomMaterialOnFile) == 8, "CustomMaterialOnFile on-disk layout drift");
const ByteOrderMask kbomCmtlf = 0x5c000000;

// material on file (Material_MTRL chunk)
struct MaterialOnFile
{
    int16_t bo;          // byte order
    int16_t osk;         // OS kind
    br_colour brc;       // RGB color
    br_ufraction brufKa; // ambient component
    br_ufraction brufKd; // diffuse component
    br_ufraction brufKs; // specular component
    byte bIndexBase;     // base of palette for this color
    byte cIndexRange;    // count of entries in palette for this color
    BRS rPower;          // specular exponent
};
static_assert(sizeof(MaterialOnFile) == 20, "MaterialOnFile on-disk layout drift");
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
    friend class CustomMaterial_CMTL;
    static ThreeMMMaterialShade _shade;
    ThreeMMMaterialResource _material;
    static ThreeMMMaterialServices _Services(void *context = nullptr);

  protected:
    Material_MTRL(void)
    {
        _material.handle = nullptr;
    } // can't instantiate directly; must use FReadMtrl
    bool _FInit(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno);

  public:
    static bool FSetShadeTable(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno);
    // Release after all materials, before shutting down BRender.
    static void ReleaseShadeTable(void);
    static PMaterial_MTRL PmtrlNew(LegacyLong iclrBase = ivNil, LegacyLong cclr = ivNil);
    static bool FReadMtrl(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck,
                          PBaseCacheableObject *ppbaco, LegacyLong *pcb);
    static PMaterial_MTRL PmtrlNewFromPix(PFilename pfni);
    static PMaterial_MTRL PmtrlNewFromBmp(PFilename pfni, PDynamicArray pglclr = pvNil);
    static PMaterial_MTRL PmtrlFromBmtl(PBMTL pbmtl);
    ~Material_MTRL(void);
    PTextureMap Ptmap(void);
    PBMTL Pbmtl(void)
    {
        return static_cast<PBMTL>(_material.handle);
    }
    bool FWrite(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber *pcno);
#ifdef DEBUG
    static void MarkShadeTable(void);
#endif // DEBUG
};

/****************************************
    The CustomMaterial_CMTL (custom material) class
    This manages a set of materials to
    apply to a body part set
****************************************/
typedef class CustomMaterial_CMTL *PCustomMaterial_CMTL;
#define CustomMaterial_CMTL_PAR BaseCacheableObject
#define kclsCustomMaterial_CMTL 'CMTL'
class CustomMaterial_CMTL : public CustomMaterial_CMTL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    friend class Material_MTRL;
    ThreeMMCustomMaterial _custom;

  protected:
    bool _FInit(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno);
    CustomMaterial_CMTL(void) : _custom{}
    {
    } // can't instantiate directly; must use PcmtlRead

  public:
    static PCustomMaterial_CMTL PcmtlNew(LegacyLong ibset, LegacyLong cbprt, PMaterial_MTRL *prgpmtrl);
    static bool FReadCmtl(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck,
                          PBaseCacheableObject *ppbaco, LegacyLong *pcb);
    static bool FHasModels(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno);
    static bool FEqualModels(PChunkyFile pcfl, ChunkNumber cno1, ChunkNumber cno2);
    ~CustomMaterial_CMTL(void);
    PBMTL Pbmtl(LegacyLong ibmtl);
    PModel Pmodl(LegacyLong imodl);
    LegacyLong Ibset(void)
    {
        return _custom.set;
    }
    LegacyLong Cbprt(void)
    {
        return _custom.count;
    }
    bool FHasModels(void);
};

#endif // !MTRL_H
