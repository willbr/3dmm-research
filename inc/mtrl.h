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
#include "material_renderer.h"

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

#endif
