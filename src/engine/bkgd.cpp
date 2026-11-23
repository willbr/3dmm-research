/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    bkgd.cpp: Background class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    A Background (background) consists of a set of light sources (GLLT), a
    background sound (SND), and one or more camera views.  Each camera view
    consists of a camera specification, a pre-rendered RGB bitmap, and a
    pre-rendered Z-buffer.  Here's how the chunks look:

    Background // Contains stage bounding cuboid
     |
     +---GLLT (chid 0) // DynamicArray of light position specs (LITEs)
     |
     +---SND  (chid 0) // Background sound/music
     |
     +---CameraPosition  (chid 0) // Contains camera pos/orient matrix, hither, yon
     |    |
     |    +---MaskedBitmapMBMP (chid 0) // Background RGB bitmap
     |    |
     |    +---ZBMP (chid 0) // Background Z-buffer
     |
     +---CameraPosition (chid 1)
     |    |
     |    +---MaskedBitmapMBMP (chid 0)
     |    |
     |    +---ZBMP (chid 0)
     |
     +---CameraPosition (chid 2)
     .    |
     .    +---MaskedBitmapMBMP (chid 0)
     .    |
          +---ZBMP (chid 0)

***************************************************************************/
#include "soc.h"
#include <cstring>
ASSERTNAME

#ifdef DEBUG
#define BACKGROUND_VALIDATE(background) EngineBackground::Validate(background)
#else
#define BACKGROUND_VALIDATE(background) ((void)0)
#endif

/***************************************************************************
    Cache the background's chunks (excluding camera views) to HD
***************************************************************************/
bool EngineBackground::FCacheToHD(PTAG ptagBkgd)
{
    AssertVarMem(ptagBkgd);
    MMBackgroundTagServices s = {};
    s.context = ptagBkgd;
    s.child = [](void *p, MMU32 type, MMTag *out) {
        TAG tag = {};
        if (!vptagm->FBuildChildTag(static_cast<PTAG>(p), 0, type, &tag))
            return 0;
        *out = TagToCore(tag);
        return 1;
    };
    s.cache = [](void *, const MMTag *input, int children) {
        TAG tag;
        TagFromCore(&tag, *input);
        return int(vptagm->FCacheTagToHD(&tag, children != 0));
    };
    auto tag = TagToCore(*ptagBkgd);
    return mm_background_cache(&tag, &s) != 0;
}

PBackground EngineBackground::Fetch(PTAG tag)
{
    return static_cast<PBackground>(vptagm->FetchResource(tag, Read, mm_background_owner_services()));
}

PBackground EngineBackground::Fetch(MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number)
{
    return mm_brender_background_fetch(cache, type, number);
}

/***************************************************************************
    Get the background's name
***************************************************************************/
void EngineBackground::GetName(PBackground background, PString pstn)
{
    BACKGROUND_VALIDATE(background);
    AssertPo(pstn, 0);

    pstn->SetSt(static_cast<PPascalString>(mm_background_owner_name(background).buffer));
}

/***************************************************************************
    Get the custom palette for this background, if any.  Returns fFalse if
    an error occurs.  Sets *ppglclr to an empty DynamicArray and *piclrMin to 0 if
    this background has no custom palette.
***************************************************************************/
bool EngineBackground::FGetPalette(PBackground background, PDynamicArray *ppglclr, long *piclrMin)
{
    BACKGROUND_VALIDATE(background);
    AssertVarMem(ppglclr);
    AssertVarMem(piclrMin);
    MMU32 count;
    MMI32 base;
    const auto colours = mm_background_palette(mm_background_owner_resource(background), &count, &base);
    *piclrMin = base;
    *ppglclr = DynamicArray::PglNew(sizeof(Color), count);
    if (*ppglclr && count)
    {
        if (!(*ppglclr)->FSetIvMac(count))
            ReleasePpo(ppglclr);
        else
            std::memcpy((*ppglclr)->QvGet(0), colours, count * sizeof(Color));
    }
    return *ppglclr != nullptr;
}

/***************************************************************************
    Get the camera position in worldspace
***************************************************************************/
void EngineBackground::GetCameraPos(PBackground background, BRS *pxr, BRS *pyr, BRS *pzr)
{
    BACKGROUND_VALIDATE(background);
    AssertNilOrVarMem(pxr);
    AssertNilOrVarMem(pyr);
    AssertNilOrVarMem(pzr);
    if (pxr)
        *pxr = Camera(background).position.dxr;
    if (pyr)
        *pyr = Camera(background).position.dyr;
    if (pzr)
        *pzr = Camera(background).position.dzr;
}

static MMBRenderBackgroundWorld WorldServices(PWorld world)
{
    return {
        world,
        [](void *p, br_actor *light) { static_cast<PWorld>(p)->AddActor(light); },
        [](void *p, MMResourceCache *cache, MMU32 rgb, MMU32 depth) -> int {
            return static_cast<PWorld>(p)->FSetBackground(cache, kctgMbmp, rgb, kctgZbmp, depth);
        },
        [](void *p, const MMBackgroundCameraData *camera) {
            BMAT34 matrix;
            std::memcpy(&matrix, &camera->matrix, sizeof(matrix));
            static_cast<PWorld>(p)->SetCamera(&matrix, camera->near_z, camera->far_z, camera->fov);
        },
        [](void *p) { static_cast<PWorld>(p)->MarkDirty(); }
    };
}

void EngineBackground::TurnOnLights(PBackground background, PWorld world)
{
    BACKGROUND_VALIDATE(background);
    AssertPo(world, 0);
    const auto services = WorldServices(world);
    mm_brender_background_lights_on(background, &services);
}

bool EngineBackground::FSetCamera(PBackground background, PWorld world, long camera)
{
    BACKGROUND_VALIDATE(background);
    AssertPo(world, 0);
    AssertIn(camera, 0, Ccam(background));
    const auto services = WorldServices(world);
    return mm_brender_background_select(background, camera, &services) != 0;
}

/***************************************************************************
    Gets the matrix for mouse-dragging relative to the camera
***************************************************************************/
void EngineBackground::GetMouseMatrix(PBackground background, BMAT34 *pbmat34)
{
    BACKGROUND_VALIDATE(background);
    AssertVarMem(pbmat34);
    static_assert(sizeof(*pbmat34) == sizeof(Camera(background).mouse), "Camera matrix layout");
    std::memcpy(pbmat34, &Camera(background).mouse, sizeof(*pbmat34));
}

/***************************************************************************
    Gets the point at which to place new actors for this bkgd/view.
***************************************************************************/
void EngineBackground::GetActorPlacePoint(PBackground background, BRS *pxr, BRS *pyr, BRS *pzr)
{
    BACKGROUND_VALIDATE(background);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);
    MMActorPoint point;
    mm_background_place(mm_background_owner_camera(background), &point);
    *pxr = point.dxr;
    *pyr = point.dyr;
    *pzr = point.dzr;
}

/******************************************************************************
    ReuseActorPlacePoint
        Resets the current actor place point to the last one used.  Call this
        from the actor placement code if the actor was placed at a point other
        than the one you just asked for.
************************************************************ PETED ***********/
void EngineBackground::ReuseActorPlacePoint(PBackground background)
{
    mm_background_reuse_place(mm_background_owner_camera(background));
}

#ifdef DEBUG
/***************************************************************************
    Authoring only.  Writes a special file with the given place info.
***************************************************************************/
bool EngineBackground::FWritePlaceFile(PBackground background, BRS xrPlace, BRS yrPlace, BRS zrPlace)
{
    BACKGROUND_VALIDATE(background);
    Assert(yrPlace == rZero, "are you sure you want non-zero Y?");

    String stnFile;
    Filename fni;
    PFileObject pfil = pvNil;
    String stnData;
    FilePosition fp;
    LegacyLong xr1 = BrScalarToInt(xrPlace);
    LegacyLong xr2 = LwAbs((long)(1000000.0 * BrScalarToFloat(xrPlace - BrIntToScalar(xr1))));
    LegacyLong yr1 = BrScalarToInt(yrPlace);
    LegacyLong yr2 = LwAbs((long)(1000000.0 * BrScalarToFloat(yrPlace - BrIntToScalar(yr1))));
    LegacyLong zr1 = BrScalarToInt(zrPlace);
    LegacyLong zr2 = LwAbs((long)(1000000.0 * BrScalarToFloat(zrPlace - BrIntToScalar(zr1))));

    String name;
    GetName(background, &name);
    if (!stnFile.FFormatSz("%s-cam.1-%d.pos", &name, static_cast<LegacyLong>(Camera(background).current + 1)))
        goto LFail;
    if (!fni.FBuildFromPath(&stnFile))
        goto LFail;
    if (fni.TExists() == tYes)
        pfil = FileObject::PfilOpen(&fni, ffilWriteEnable);
    else
        pfil = FileObject::PfilCreate(&fni);
    if (pvNil == pfil)
        goto LFail;
    if (!stnData.FFormatSz("NEW_ACTOR_POS %d.%06d %d.%06d %d.%06d\n\r", xr1, xr2, yr1, yr2, zr1, zr2))
    {
        goto LFail;
    }
    if ((fp = pfil->FpMac()) > 0)
    {
        // Go to the end of the file (and write over null byte at end
        // of previous string)
        fp--;
    }
    if (!pfil->FWriteRgb(stnData.Psz(), stnData.Cch() + 1, fp))
        goto LFail;
    ReleasePpo(&pfil);
    return fTrue;
LFail:
    ReleasePpo(&pfil);
    return fFalse;
}
#endif // DEBUG

#ifdef DEBUG
void EngineBackground::Validate(PBackground background)
{
    mm_kauai_background_validate(background);
}

void EngineBackground::Mark(PBackground background)
{
    mm_kauai_background_mark(background);
}
#endif // DEBUG
