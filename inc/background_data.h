/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef BACKGROUND_DATA_H
#define BACKGROUND_DATA_H
#include "bren_core.h"
#include <cstdint>
using namespace BRender;

/****************************************
    Background on file
****************************************/
struct BackgroundFile
{
    int16_t bo;
    int16_t osk;
    byte bIndexBase;
    byte bPad;
    int16_t swPad;
};
static_assert(sizeof(BackgroundFile) == 8, "BackgroundFile on-disk layout drift");
const ByteOrderMask kbomBkgdf = 0x50000000;

/****************************************
    Specifies a light's kind, position,
    orientation, and brightness
****************************************/
struct LightPosition
{
    BMAT34 bmat34;
    BRS rIntensity;
    int32_t lt; // light type
};
static_assert(sizeof(LightPosition) == 56, "LightPosition on-disk layout drift");
const ByteOrderMask kbomLite = 0xfffffff0;

/****************************************
    Specifies a camera for a view
****************************************/
typedef union _apos {
    struct
    {
        BRS xrPlace; // Initial Actor Placement point
        BRS yrPlace;
        BRS zrPlace;
    };
    BVEC3 bvec3Actor;
} APOS;

struct CameraPosition
{
    int16_t bo;
    int16_t osk;
    BRS zrHither; // Hither (near) plane
    BRS zrYon;    // Yon (far) plane
    BRA aFov;     // Field of view
    int16_t swPad;
    APOS apos;
    BMAT34 bmat34Cam; // Camera view matrix
    // APOS rgapos[];
};
static_assert(sizeof(CameraPosition) == 76, "CameraPosition on-disk layout drift");
const ByteOrderMask kbomCamOld = 0x5f4fc000;
const ByteOrderMask kbomCam = BomField(
    kbomSwapShort,
    BomField(kbomSwapShort,
             BomField(kbomSwapLong,
                      BomField(kbomSwapLong,
                               BomField(kbomSwapShort,
                                        BomField(kbomLeaveShort,
                                                 BomField(kbomSwapLong,
                                                          BomField(kbomSwapLong, BomField(kbomSwapLong, 0)))))))));

// Note that CameraPosition is too big for a complete kbomCam.  To SwapBytes one,
// SwapBytesBom the cam, then SwapBytesRgLw from bmat34Cam on.

#endif
