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
#include "engine_movie_graph.h"
#include <cstring>
ASSERTNAME

RTCLASS(Background)

const ChildChunkID kchidBds = 0;  // Background default sound
const ChildChunkID kchidGllt = 0; // DynamicArray of LITEs
const ChildChunkID kchidGlcr = 0; // Palette
const br_colour kbrcLight = BR_COLOUR_RGB(0xff, 0xff, 0xff);

namespace
{
struct BackgroundResourceHost
{
    Background *background;
    PChunkyFile file;
    PWorld world;
    DataBlock block;
};
} // namespace
MMBackgroundServices Background::_ResourceServices(void *host)
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
    s.child = [](void *p, MMU32 type, MMU32 number, MMU32 id, MMU32 childType, MMU32 *out) {
        const auto chunks = static_cast<BackgroundResourceHost *>(p)->file->ReadChunkAccess();
        return int(mm_chunk_link_number(&chunks, type, number, id, childType, out) == MM_CHUNK_OK);
    };
    s.child_at = [](void *p, MMU32 type, MMU32 number, MMI32 index, MMU32 *out) {
        ChildChunkIdentification child;
        if (!static_cast<BackgroundResourceHost *>(p)->file->FGetKid(type, number, index, &child))
            return 0;
        *out = child.cki.ctg;
        return 1;
    };
    s.find = [](void *p, MMU32 type, MMU32 number, int unpack) {
        auto h = static_cast<BackgroundResourceHost *>(p);
        return int(h->file->FFind(type, number, &h->block) && (!unpack || h->block.FUnpackData()));
    };
    s.stream = [](void *p) { return EngineReadStream::FromBlock(static_cast<BackgroundResourceHost *>(p)->block); };
    s.name = [](void *p, MMU32 type, MMU32 number) {
        auto h = static_cast<BackgroundResourceHost *>(p);
        return int(h->file->FGetName(type, number, &h->background->_stn));
    };
    s.read_list = [](void *p, int *big) -> void * {
        auto h = static_cast<BackgroundResourceHost *>(p);
        short order = kboCur;
        auto list = DynamicArray::PglRead(&h->block, &order);
        const unsigned short one = 1;
        *big = (*reinterpret_cast<const byte *>(&one) == 0) ^ (order == kboOther);
        return list;
    };
    s.count = [](void *, void *p) -> MMI32 { return static_cast<PDynamicArray>(p)->IvMac(); };
    s.stride = [](void *, void *p) -> MMI32 { return static_cast<PDynamicArray>(p)->CbEntry(); };
    s.get = [](void *, void *p, MMI32 index, void *out, size_t bytes) {
        std::memcpy(out, static_cast<PDynamicArray>(p)->QvGet(index), bytes);
    };
    s.put = [](void *, void *p, MMI32 index, const void *in, size_t bytes) {
        std::memcpy(static_cast<PDynamicArray>(p)->QvGet(index), in, bytes);
    };
    s.release = [](void *, void *p) { static_cast<PDynamicArray>(p)->Release(); };
    s.allocate = [](void *, MMI32 bytes, void **out) { return int(FAllocPv(out, bytes, fmemClear, mprNormal)); };
    s.free_memory = [](void *, void **out) { FreePpv(out); };
    s.configure_light = [](void *, MMBackgroundResource *state, MMI32 index,
                           const MMBackgroundLight *light) {
        auto actor = &static_cast<BACT *>(state->actors)[index];
        auto data = &static_cast<BLIT *>(state->lights)[index];
        data->type = static_cast<byte>(light->type);
        data->colour = kbrcLight;
        data->attenuation_c = light->intensity;
        actor->type = BR_ACTOR_LIGHT;
        actor->type_data = data;
        actor->t.type = BR_TRANSFORM_MATRIX34;
        static_assert(sizeof(actor->t.t.mat) == sizeof(light->matrix), "Light matrix layout");
        std::memcpy(&actor->t.t.mat, &light->matrix, sizeof(light->matrix));
        return 1;
    };
    s.add_light = [](void *p, MMBackgroundResource *b, MMI32 i) {
        static_cast<BackgroundResourceHost *>(p)->world->AddActor(&static_cast<BACT *>(b->actors)[i]);
    };
    s.enable_light = [](void *, MMBackgroundResource *b, MMI32 i) {
        BrLightEnable(&static_cast<BACT *>(b->actors)[i]);
    };
    s.disable_light = [](void *, MMBackgroundResource *b, MMI32 i) {
        BrLightDisable(&static_cast<BACT *>(b->actors)[i]);
    };
    s.remove_light = [](void *, MMBackgroundResource *b, MMI32 i) {
        BrActorRemove(&static_cast<BACT *>(b->actors)[i]);
    };
    s.new_palette = [](void *) -> void * { return DynamicArray::PglNew(sizeof(Color)); };
    s.duplicate_palette = [](void *, void *palette) -> void * { return static_cast<PDynamicArray>(palette)->PglDup(); };
    s.diagnostic = [](void *, int issue) {
        if (issue == MM_BACKGROUND_CAMERA_GAPS)
            Bug("cam chids are not consecutive!");
        else
            Warn("Error reading background");
    };
    return s;
}

/***************************************************************************
    Add the background's chunks (excluding camera views) to the tag list
***************************************************************************/
bool Background::FAddTagsToTagl(PTAG ptagBkgd, PTagList ptagl)
{
    AssertVarMem(ptagBkgd);
    AssertPo(ptagl, 0);
    struct Host
    {
        PTAG tag;
        PTagList list;
    } host = {ptagBkgd, ptagl};
    MMBackgroundTagServices s = {};
    s.context = &host;
    s.insert = [](void *p, int parent, MMU32 type) {
        auto h = static_cast<Host *>(p);
        return int(parent ? h->list->FInsertTag(h->tag, fFalse) : h->list->FInsertChild(h->tag, 0, type));
    };
    return mm_background_tags(&s) != 0;
}

/***************************************************************************
    Cache the background's chunks (excluding camera views) to HD
***************************************************************************/
bool Background::FCacheToHD(PTAG ptagBkgd)
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
    A PFNRPO to read a Background from a file
***************************************************************************/
bool Background::FReadBkgd(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck,
                           PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);
    struct Host
    {
        PChunkyResourceFile cache;
        PBaseCacheableObject *object;
        LegacyLong *bytes;
    } host = {pcrf, ppbaco, pcb};
    MMBackgroundFactoryServices s = {};
    s.context = &host;
    s.object_bytes = sizeof(Background);
    s.actor_bytes = sizeof(BACT);
    s.light_bytes = sizeof(BLIT);
    s.create = [](void *) -> void * { return NewObj Background; };
    s.initialize = [](void *p, void *object, MMU32 type, MMU32 number) {
        return int(static_cast<Background *>(object)->_FInit(static_cast<Host *>(p)->cache->Pcfl(), type, number));
    };
    s.lights = [](void *, void *object) -> MMI32 {
        return static_cast<Background *>(object)->_resource.light_count;
    };
    s.release = [](void *, void *object) { static_cast<Background *>(object)->Release(); };
    s.size = [](void *p, MMI32 bytes) { *static_cast<Host *>(p)->bytes = bytes; };
    s.publish = [](void *p, void *object) {
        auto background = static_cast<Background *>(object);
        AssertPo(background, 0);
        *static_cast<Host *>(p)->object = background;
    };
    s.failure = [](void *p) {
        auto h = static_cast<Host *>(p);
        TrashVar(h->object);
        TrashVar(h->bytes);
    };
    return mm_background_factory(ctg, cno, ppbaco != nullptr, &s) != 0;
}

/***************************************************************************
    Read a Background from the given chunk of the given ChunkyFile.
    Note: Although we read the data for the lights here, we don't turn
    them on yet because we don't have a World to add them to.  The lights
    are	turned on with the first FSetCamera() call.
***************************************************************************/
bool Background::_FInit(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);
    BackgroundResourceHost host = {this, pcfl, nullptr};
    auto services = _ResourceServices(&host);
    return mm_background_read(&_resource, &_camera, ctg, cno, MM_BACKGROUND_STUDIO, &services) != 0;
}

/***************************************************************************
    Clean up and delete this background
***************************************************************************/
Background::~Background(void)
{
    AssertBaseThis(0);
    Assert(!_resource.leave_on, "Shouldn't be freeing background now");
    auto services = _ResourceServices(nullptr);
    mm_background_resource_dispose(&_resource, &services);
    auto camera = _CameraServices(nullptr);
    mm_background_camera_dispose(&_camera, &camera);
}

/***************************************************************************
    Get the background's name
***************************************************************************/
void Background::GetName(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    *pstn = _stn;
}

/***************************************************************************
    Get the custom palette for this background, if any.  Returns fFalse if
    an error occurs.  Sets *ppglclr to an empty DynamicArray and *piclrMin to 0 if
    this background has no custom palette.
***************************************************************************/
bool Background::FGetPalette(PDynamicArray *ppglclr, long *piclrMin)
{
    AssertThis(0);
    AssertVarMem(ppglclr);
    AssertVarMem(piclrMin);
    *piclrMin = _resource.palette_base;
    MMI32 base;
    auto services = _ResourceServices(nullptr);
    *ppglclr = static_cast<PDynamicArray>(mm_background_palette(&_resource, &base, &services));
    return *ppglclr != nullptr;
}

/***************************************************************************
    Get the camera position in worldspace
***************************************************************************/
void Background::GetCameraPos(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertNilOrVarMem(pxr);
    AssertNilOrVarMem(pyr);
    AssertNilOrVarMem(pzr);
    if (pxr)
        *pxr = _camera.position.dxr;
    if (pyr)
        *pyr = _camera.position.dyr;
    if (pzr)
        *pzr = _camera.position.dzr;
}

/***************************************************************************
    Turn on lights in pbwld
***************************************************************************/
void Background::TurnOnLights(PWorld pbwld)
{
    AssertThis(0);
    AssertPo(pbwld, 0);
    BackgroundResourceHost host = {this, nullptr, pbwld};
    auto services = _ResourceServices(&host);
    mm_background_lights_on(&_resource, &services);
}

/***************************************************************************
    Turn off lights in pbwld
***************************************************************************/
void Background::TurnOffLights(void)
{
    AssertThis(0);
    auto services = _ResourceServices(nullptr);
    mm_background_lights_off(&_resource, &services);
}

/***************************************************************************
    Set the camera and associated bitmaps to icam
***************************************************************************/
namespace
{
struct BackgroundCameraHost
{
    Background *background;
    PWorld world;
};
} // namespace
MMBackgroundCameraServices Background::_CameraServices(void *host)
{
    MMBackgroundCameraServices s = {};
    s.context = host;
    s.background = [](void *p, MMU32 rgb, MMU32 depth) {
        auto h = static_cast<BackgroundCameraHost *>(p);
        return int(h->world->FSetBackground(h->background->Pcrf(), kctgMbmp, rgb, kctgZbmp, depth));
    };
    s.new_points = [](void *, MMI32 count) -> void * {
        auto points = DynamicArray::PglNew(sizeof(APOS), count);
        if (points)
            AssertDo(points->FSetIvMac(count), "Should never fail");
        return points;
    };
    s.release = [](void *, void *points) { static_cast<PDynamicArray>(points)->Release(); };
    s.lock = [](void *, void *points) { static_cast<PDynamicArray>(points)->Lock(); };
    s.unlock = [](void *, void *points) { static_cast<PDynamicArray>(points)->Unlock(); };
    s.put_point = [](void *, void *points, MMI32 index, const MMActorPoint *point) {
        APOS value;
        value.xrPlace = point->dxr;
        value.yrPlace = point->dyr;
        value.zrPlace = point->dzr;
        static_cast<PDynamicArray>(points)->Put(index, &value);
    };
    s.point = [](void *, void *points, MMI32 index, MMActorPoint *out) {
        APOS value;
        static_cast<PDynamicArray>(points)->Get(index, &value);
        *out = {value.xrPlace, value.yrPlace, value.zrPlace};
    };
    s.point_count = [](void *, void *points) -> MMI32 { return static_cast<PDynamicArray>(points)->IvMac(); };
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

bool Background::FSetCamera(PWorld pbwld, long icam)
{
    AssertThis(0);
    AssertPo(pbwld, 0);
    AssertIn(icam, 0, Ccam());
    BackgroundCameraHost host = {this, pbwld};
    const auto services = _CameraServices(&host);
    struct Selection
    {
        MMBackgroundCameraState *state;
        MMI32 slot;
        const MMBackgroundCameraServices *services;
    } selection = {&_camera, static_cast<MMI32>(icam), &services};
    TurnOnLights(pbwld);
    const auto graph = EngineMovieGraph::FromFile(Pcrf()->Pcfl());
    return mm_movie_camera_graph(&graph, Cno(), icam,
        [](void *p, const MMBackgroundCameraBinding *binding, const MMReadStream *stream) -> int {
            auto &s = *static_cast<Selection *>(p);
            return mm_background_camera_apply(s.state, s.slot, binding, stream, s.services)
                       ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
        }, &selection, [](void *p, int issue) {
            const auto &s = *static_cast<Selection *>(p);
            s.services->diagnostic(s.services->context, issue);
        }) == MM_MOVIE_OK;
}

/***************************************************************************
    Gets the matrix for mouse-dragging relative to the camera
***************************************************************************/
void Background::GetMouseMatrix(BMAT34 *pbmat34)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    static_assert(sizeof(*pbmat34) == sizeof(_camera.mouse), "Camera matrix layout");
    std::memcpy(pbmat34, &_camera.mouse, sizeof(*pbmat34));
}

/***************************************************************************
    Gets the point at which to place new actors for this bkgd/view.
***************************************************************************/
void Background::GetActorPlacePoint(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);
    auto services = _CameraServices(nullptr);
    MMActorPoint point;
    mm_background_place(&_camera, &point, &services);
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
void Background::ReuseActorPlacePoint(void)
{
    mm_background_reuse_place(&_camera);
}

#ifdef DEBUG
/***************************************************************************
    Authoring only.  Writes a special file with the given place info.
***************************************************************************/
bool Background::FWritePlaceFile(BRS xrPlace, BRS yrPlace, BRS zrPlace)
{
    AssertThis(0);
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

    if (!stnFile.FFormatSz("%s-cam.1-%d.pos", &_stn, static_cast<LegacyLong>(_camera.current + 1)))
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
void Background::AssertValid(LegacyUlong grf)
{
    Background_PAR::AssertValid(fobjAllocated);
    AssertIn(_resource.light_count, 1, 100); // 100 is sanity check
    AssertIn(_camera.count, 1, 100);         // 100 is sanity check
    Assert(_camera.current == ivNil || (_camera.current >= 0 && _camera.current < _camera.count),
           "bad _camera.current");
    AssertPvCb(_resource.actors, LwMul(_resource.light_count, size(BACT)));
    AssertPvCb(_resource.lights, LwMul(_resource.light_count, size(BLIT)));
    AssertPo(&_stn, 0);
    AssertNilOrPo(static_cast<PDynamicArray>(_camera.points), 0);
}

/***************************************************************************
    Mark memory used by the Background
***************************************************************************/
void Background::MarkMem(void)
{
    AssertThis(0);
    Background_PAR::MarkMem();
    MarkPv(_resource.actors);
    MarkPv(_resource.lights);
    MarkMemObj(static_cast<PDynamicArray>(_resource.palette));
    MarkMemObj(static_cast<PDynamicArray>(_camera.points));
}
#endif // DEBUG
