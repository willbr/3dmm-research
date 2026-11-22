/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/*************************************************************************

    mtrl.h: BRender material and costume bindings

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

*************************************************************************/
#ifndef MTRL_H
#define MTRL_H

#include <cstdint>
#include "asset_resources.h"

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

using PMaterial_MTRL = MMMaterialOwner *;
using PCustomMaterial_CMTL = MMMaterialOwner *;

namespace EngineMaterial
{
const MMMaterialOwnerHost &Host();
constexpr MMResourceBlockReader Read = mm_brender_material_read;
PMaterial_MTRL Fetch(MMResourceCache *, ChunkTagOrType, ChunkNumber);
PMaterial_MTRL Fetch(PChunkyResourceFile, ChunkTagOrType, ChunkNumber);
bool SetShadeTable(PChunkyFile, ChunkTagOrType, ChunkNumber);
bool SetShadeTable(MMResourceCache *, ChunkTagOrType, ChunkNumber);
bool SetShadeTable(const MMReadStream &);
void ReleaseShadeTable();
PMaterial_MTRL New(LegacyLong base = ivNil, LegacyLong range = ivNil);
PMaterial_MTRL FromPix(PFilename);
PMaterial_MTRL FromBitmap(PFilename, PDynamicArray palette = nullptr);
PMaterial_MTRL FromHandle(PBMTL);
PBMTL Handle(PMaterial_MTRL);
PTextureMap Texture(PMaterial_MTRL);
bool Write(PMaterial_MTRL, PChunkyFile, ChunkTagOrType, ChunkNumber *,
           const MMMaterialDescription *appearance = nullptr, const MMI32 mapping[6] = nullptr);
bool Write(PMaterial_MTRL, const MMArchiveWriter &, ChunkTagOrType, ChunkNumber *,
           const MMMaterialDescription *appearance = nullptr, const MMI32 mapping[6] = nullptr);
void ReadView(PMaterial_MTRL, MMMaterialAppearance &);
bool WriteView(PMaterial_MTRL, const MMArchiveWriter &, ChunkTagOrType, ChunkNumber *, const MMMaterialAppearance &);
#ifdef DEBUG
void Validate(PMaterial_MTRL);
void Mark(MMMaterialOwner *);
void MarkShadeTable();
#endif
}

namespace EngineCostume
{
constexpr MMResourceBlockReader Read = mm_brender_costume_read;
PCustomMaterial_CMTL Fetch(MMResourceCache *, ChunkTagOrType, ChunkNumber);
PCustomMaterial_CMTL Fetch(PChunkyResourceFile, ChunkTagOrType, ChunkNumber);
PCustomMaterial_CMTL New(LegacyLong, LegacyLong, PMaterial_MTRL *);
bool HasModels(PChunkyFile, ChunkTagOrType, ChunkNumber);
bool EqualModels(PChunkyFile, ChunkNumber, ChunkNumber);
PBMTL Handle(PCustomMaterial_CMTL, LegacyLong);
PModel Model(PCustomMaterial_CMTL, LegacyLong);
LegacyLong Set(PCustomMaterial_CMTL);
LegacyLong Count(PCustomMaterial_CMTL);
bool HasModels(PCustomMaterial_CMTL);
#ifdef DEBUG
void Validate(PCustomMaterial_CMTL);
#endif
}

#endif
