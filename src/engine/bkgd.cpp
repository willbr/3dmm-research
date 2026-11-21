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
#include "handle_services.h"
#include "scene_renderer.h"
#include <cstring>
ASSERTNAME

#ifdef DEBUG
#define BACKGROUND_VALIDATE(background) EngineBackground::Validate(background)
#else
#define BACKGROUND_VALIDATE(background) ((void)0)
#endif

const ChildChunkID kchidBds = 0;  // Background default sound
const ChildChunkID kchidGllt = 0; // DynamicArray of LITEs
const ChildChunkID kchidGlcr = 0; // Palette

namespace
{
struct BackgroundResourceHost
{
    PBackground background;
    PChunkyResourceFile cache;
    PWorld world;
};
} // namespace
static MMBackgroundServices ResourceServices(void *host)
{
    MMBackgroundServices s = {};
    s.context = host;
    const unsigned short one = 1;
    s.host_big_endian = *reinterpret_cast<const byte *>(&one) == 0;
#ifdef DEBUG
    s.debug_checks = 1;
#endif
    s.actor_bytes = sizeof(BACT);
    s.light_bytes = sizeof(BLIT);
    s.name = [](void *p, MMU32 type, MMU32 number) {
        auto h = static_cast<BackgroundResourceHost *>(p);
        String name;
        const bool found = h->cache->FGetName(type, number, &name);
        const auto storage = mm_background_owner_name(h->background);
        mm_string_value_copy(&storage, name.Pst());
        return int(found);
    };
    s.allocate = [](void *, MMI32 bytes, void **out) { return int(FAllocPv(out, bytes, fmemClear, mprNormal)); };
    s.free_memory = [](void *, void **out) { FreePpv(out); };
    s.configure_light = mm_brender_background_light;
    s.add_light = [](void *p, MMBackgroundResource *b, MMI32 i) {
        static_cast<BackgroundResourceHost *>(p)->world->AddActor(&static_cast<BACT *>(b->actors)[i]);
    };
    s.enable_light = mm_brender_background_light_enable;
    s.disable_light = mm_brender_background_light_disable;
    s.remove_light = mm_brender_background_light_remove;
    s.diagnostic = [](void *, int issue) {
        if (issue == MM_BACKGROUND_CAMERA_GAPS)
            Bug("cam chids are not consecutive!");
        else
            Warn("Error reading background");
    };
    return s;
}

const MMBackgroundOwnerHost &EngineBackground::Host()
{
    static const auto teardown = ResourceServices(nullptr);
    static const MMBackgroundOwnerHost host = {
        KauaiResourceObjectAllocator(), &teardown, sizeof(achar),
#ifdef DEBUG
        EngineBackground::Mark
#else
        nullptr
#endif
    };
    return host;
}

static MMI32 LegacyBackgroundSize()
{
    // Preserve the cache charge of the former native background class.
    size_t prefix = sizeof(void *) == 8 ? 40 : 28;
#ifdef DEBUG
    if (sizeof(void *) == 4)
        prefix += 4;
#endif
    const size_t bytes = prefix + sizeof(MMBackgroundResource) + sizeof(MMBackgroundCameraState) + sizeof(String);
    const size_t alignment = sizeof(void *);
    return static_cast<MMI32>((bytes + alignment - 1) & ~(alignment - 1));
}

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

/***************************************************************************
    Read a Background through its resource cache.
***************************************************************************/
int EngineBackground::Read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                         void **object, MMI32 *bytes)
{
    struct Host
    {
        MMResourceCache *cache;
        MMOwnedBlock *block;
        void **object;
        MMI32 *bytes;
    } host = {cache, block, object, bytes};
    MMBackgroundFactoryServices s = {};
    s.context = &host;
    s.object_bytes = LegacyBackgroundSize();
    s.actor_bytes = sizeof(BACT);
    s.light_bytes = sizeof(BLIT);
    s.create = [](void *) -> void * { return mm_background_owner_create(&EngineBackground::Host()); };
    s.initialize = [](void *p, void *object, MMU32 type, MMU32 number) {
        auto host = static_cast<Host *>(p);
        auto background = static_cast<PBackground>(object);
        BackgroundResourceHost resource = {background, static_cast<PChunkyResourceFile>(host->cache->owner), nullptr};
        const auto services = ResourceServices(&resource);
        return mm_background_read_cached(mm_background_owner_resource(background),
                                         mm_background_owner_camera(background), type, number,
                                         host->cache, host->block, KauaiDecodeBlockHandle, &services);
    };
    s.lights = [](void *, void *object) -> MMI32 {
        return Resource(static_cast<PBackground>(object)).light_count;
    };
    s.release = [](void *, void *object) { mm_background_owner_release(static_cast<PBackground>(object)); };
    s.size = [](void *p, MMI32 bytes) { *static_cast<Host *>(p)->bytes = bytes; };
    s.publish = [](void *p, void *object) {
        auto background = static_cast<PBackground>(object);
        BACKGROUND_VALIDATE(background);
        *static_cast<Host *>(p)->object = background;
    };
    s.failure = [](void *p) {
        auto h = static_cast<Host *>(p);
        TrashVar(h->object);
        TrashVar(h->bytes);
    };
    return mm_background_factory(type, number, object != nullptr, &s);
}

PBackground EngineBackground::Fetch(PTAG tag)
{
    return static_cast<PBackground>(vptagm->FetchResource(tag, Read, mm_background_owner_services()));
}

PBackground EngineBackground::Fetch(PChunkyResourceFile file, ChunkTagOrType type, ChunkNumber number)
{
    return static_cast<PBackground>(mm_resource_cache_fetch_owned_chunk(file->CoreCache(), type, number, Read,
                                                                        mm_background_owner_services()));
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

/***************************************************************************
    Turn on lights in pbwld
***************************************************************************/
void EngineBackground::TurnOnLights(PBackground background, PWorld pbwld)
{
    BACKGROUND_VALIDATE(background);
    AssertPo(pbwld, 0);
    BackgroundResourceHost host = {background, nullptr, pbwld};
    auto services = ResourceServices(&host);
    mm_background_lights_on(mm_background_owner_resource(background), &services);
}

/***************************************************************************
    Turn off lights in pbwld
***************************************************************************/
void EngineBackground::TurnOffLights(PBackground background)
{
    BACKGROUND_VALIDATE(background);
    auto services = ResourceServices(nullptr);
    mm_background_lights_off(mm_background_owner_resource(background), &services);
}

/***************************************************************************
    Set the camera and associated bitmaps to icam
***************************************************************************/
namespace
{
struct BackgroundCameraHost
{
    PBackground background;
    PWorld world;
};
} // namespace
static MMBackgroundCameraServices CameraServices(void *host)
{
    MMBackgroundCameraServices s = {};
    s.context = host;
    s.background = [](void *p, MMU32 rgb, MMU32 depth) {
        auto h = static_cast<BackgroundCameraHost *>(p);
        const auto binding = mm_background_owner_binding(h->background);
        auto cache = static_cast<PChunkyResourceFile>(binding->cache->owner);
        return int(h->world->FSetBackground(cache, kctgMbmp, rgb, kctgZbmp, depth));
    };
    s.euler_yxy = [](void *, const MMActorMatrix *value, unsigned short *a, unsigned short *c) {
        BMAT34 matrix;
        std::memcpy(&matrix, value, sizeof(matrix));
        BREUL angles;
        angles.order = BR_EULER_YXY_R;
        BrMatrix34ToEuler(&angles, &matrix);
        *a = angles.a;
        *c = angles.c;
    };
    s.rotate_y = [](void *, MMActorMatrix *out, unsigned short yaw) {
        BMAT34 matrix;
        BrMatrix34RotateY(&matrix, yaw);
        std::memcpy(out, &matrix, sizeof(matrix));
    };
    s.translate = [](void *, MMActorMatrix *out, const MMActorPoint *point) {
        BMAT34 matrix;
        std::memcpy(&matrix, out, sizeof(matrix));
        BrMatrix34PostTranslate(&matrix, point->dxr, point->dyr, point->dzr);
        std::memcpy(out, &matrix, sizeof(matrix));
    };
    s.camera = [](void *p, const MMBackgroundCameraData *camera) {
        auto h = static_cast<BackgroundCameraHost *>(p);
        BMAT34 matrix;
        std::memcpy(&matrix, &camera->matrix, sizeof(matrix));
        h->world->SetCamera(&matrix, camera->near_z, camera->far_z, camera->fov);
    };
    s.dirty = [](void *p) { static_cast<BackgroundCameraHost *>(p)->world->MarkDirty(); };
    s.diagnostic = [](void *, int issue) {
        if (issue == MM_BACKGROUND_CAMERA_SHORT)
            Bug("CameraPosition chunk not large enough");
        else
            Bug("CameraPosition chunk's extra data not an even multiple of size(APOS)");
    };
    return s;
}

bool EngineBackground::FSetCamera(PBackground background, PWorld pbwld, long icam)
{
    BACKGROUND_VALIDATE(background);
    AssertPo(pbwld, 0);
    AssertIn(icam, 0, Ccam(background));
    BackgroundCameraHost host = {background, pbwld};
    const auto services = CameraServices(&host);
    TurnOnLights(background, pbwld);
    const auto binding = mm_background_owner_binding(background);
    return mm_background_select_cached(mm_background_owner_camera(background), binding->cache, binding->number,
                                       icam, KauaiDecodeBlockHandle, &services) != 0;
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
/***************************************************************************
    Assert the validity of the Background.
***************************************************************************/
void EngineBackground::Validate(PBackground background)
{
    if (!background)
        return;
    AssertIn(Resource(background).light_count, 1, 100); // 100 is sanity check
    AssertIn(Camera(background).count, 1, 100);         // 100 is sanity check
    Assert(Camera(background).current == ivNil || (Camera(background).current >= 0 && Camera(background).current < Camera(background).count),
           "bad Camera(background).current");
    AssertPvCb(Resource(background).actors, LwMul(Resource(background).light_count, size(BACT)));
    AssertPvCb(Resource(background).lights, LwMul(Resource(background).light_count, size(BLIT)));
}

/***************************************************************************
    Mark memory used by the Background
***************************************************************************/
void EngineBackground::Mark(PBackground background)
{
    if (!background)
        return;
    Validate(background);
    MarkPv(background);
    const auto binding = mm_background_owner_binding(background);
    if (binding->cache && !binding->attached)
        MarkMemObj(static_cast<PChunkyResourceFile>(binding->cache->owner));
    MarkPv(Resource(background).actors);
    MarkPv(Resource(background).lights);
}
#endif // DEBUG
