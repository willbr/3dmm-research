/***************************************************************************

    bwld.cpp: BRender world class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    The C89 buffer owner defines the working resolution and full viewport.
    Dirty regions use working-buffer coordinates until marking the screen,
    when the renderer translates them to viewport coordinates.

***************************************************************************/
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include "bren.h"
#include "scene_renderer.h"

ASSERTNAME

namespace BRender {

#ifdef WIN
// GUI diagnostics belong to the World adapter; core consumers use stderr.
static void BR_CALLBACK WorldWarning(char *message)
{
    AppendCrashLog("BRENDER_WARN", message);
    MessageBox(0, message, "BRender Warning", MB_OK);
}

static void BR_CALLBACK WorldError(char *message)
{
    AppendCrashLog("BRENDER_FATAL", message);
    MessageBox(0, message, "BRender Fatal Error", MB_OK);
    std::exit(10);
}

static br_diaghandler WorldDiagHandler = {"World DiagHandler", WorldWarning, WorldError};
static br_diaghandler *PreviousDiagHandler;
#endif

RTCLASS(World)

bool World::_fBRenderInited = fFalse;
bool World::_fRenderWireframe = fFalse;
bool World::_fFlatShading = fFalse;
bool World::_fNoTexture = fFalse;
long World::_defaultRenderScale = 1;

struct WorldRenderContext
{
    PWorld world;
    MMStorageAllocator region_allocator = Group::CollectionAllocator();
    PGraphicsEnvironment destination = nullptr;
    PGraphicsObject gob = nullptr;
    PFilename filename = nullptr;
};
static PWorld RenderWorld(void *p)
{
    return static_cast<WorldRenderContext *>(p)->world;
}
static RC RenderRect(const MMI32 r[4])
{
    return RC(r[0], r[1], r[2], r[3]);
}
static void RenderRectOut(const RC &r, MMI32 out[4])
{
    out[0] = r.xpLeft;
    out[1] = r.ypTop;
    out[2] = r.xpRight;
    out[3] = r.ypBottom;
}
void World::_SyncPixelmaps()
{
    mm_brender_pixelmap_bind(&_bpmpRGB, &_buffers.rgb);
    mm_brender_pixelmap_bind(&_bpmpZ, &_buffers.z);
}

MMWorldRenderServices World::_RenderServices(void *context, MMMovieSession *movie, const MMMovieLiveFrame *position)
{
    MMWorldRenderServices s = {};
    mm_brender_world_services(&s);
    s.context = context;
    s.movie = movie;
    s.position = position;
    _SyncPixelmaps();
    mm_render_frame_borrow(&s.frame, &_bactWorld, &_bactCamera, &_bpmpRGB, &_bpmpZ);
    mm_brender_world_camera_view(&_bactCamera, &_camera, &s.camera);
    if (_pfnreadbody && _pfnpreparebody)
    {
        s.body = [](void *p, void *actor, MMRenderBody *out) -> int {
            return RenderWorld(p)->_pfnreadbody(static_cast<PBACT>(actor), out);
        };
        s.prepare_body = [](void *p, const MMRenderBody *body) { RenderWorld(p)->_pfnpreparebody(body); };
        s.retain_body = [](void *, void *body) { mm_body_owner_retain(static_cast<MMBodyOwner *>(body)); };
        s.release_body = [](void *, void *body) { mm_body_owner_release(static_cast<MMBodyOwner *>(body)); };
    }
    s.has = [](void *p, int kind) -> int {
        auto w = RenderWorld(p);
        return kind == MM_WORLD_BEGIN ? w->_pfnbeginrend != nullptr : w->_pfngetrect != nullptr;
    };
    s.begin = [](void *p, void *actor) { RenderWorld(p)->_pfnbeginrend(static_cast<PBACT>(actor)); };
    s.rect = [](void *p, void *actor, MMI32 out[4]) {
        RC r;
        RenderWorld(p)->_pfngetrect(static_cast<PBACT>(actor), &r);
        RenderRectOut(r, out);
    };
    s.options = [](void *, int *wire, int *flat, int *noTexture) {
        *wire = World::RenderWireframe();
        *flat = World::FlatShading();
        *noTexture = World::NoTexture();
    };
    s.region_allocator = &static_cast<WorldRenderContext *>(context)->region_allocator;
    s.flush = [](void *) { GraphicsPort::Flush(); };
    s.screen_mark = [](void *p, const MMRegion *region) {
        auto c = static_cast<WorldRenderContext *>(p);
        vpappb->MarkRegion(region, c->gob);
    };
    s.draw = [](void *p, const MMPixelSurface *source, const MMI32 destination[4]) {
        auto c = static_cast<WorldRenderContext *>(p);
        auto dst = RenderRect(destination);
        c->destination->CopyIndexedPixels(*source, &dst);
    };
    return s;
}

void World::SetNoTexture(bool fOn)
{
    _fNoTexture = fOn;
}

MMWorldBufferServices World::_BufferServices()
{
    MMWorldBufferServices s = {};
    s.context = this;
    s.rgb_type = BR_PMT_INDEX_8;
    s.depth_type = BR_PMT_DEPTH_16;
    s.depth_host = DepthBitmapHost();
    s.read_colour = ReadCachedMaskedBitmap;
    s.read_depth = ReadCachedDepthBitmap;
    s.setup_scene = [](void *p) {
        auto w = static_cast<PWorld>(p);
        mm_brender_world_init(&w->_bactWorld, &w->_bactCamera, &w->_bcam, w, w->_buffers.width, w->_buffers.height);
    };
    s.changed = [](void *p) { static_cast<PWorld>(p)->MarkDirty(); };
    return s;
}

/***************************************************************************
    Recursively force a render style on every BR_ACTOR_MODEL descendant.
    Used by the wireframe override: setting only the world root is not
    sufficient because body parts assign their own non-default render_style
    (BR_RSTYLE_FACES), which BRender treats as overriding the inherited one.
***************************************************************************/
/***************************************************************************
    Allocate a new BRender world
***************************************************************************/
PWorld World::PbwldNew(long dxp, long dyp, bool fHalfX, bool fHalfY)
{
    AssertIn(dxp, 1, ksuMax); // BPMP's width and height are ushorts
    AssertIn(dyp, 1, ksuMax);
    Assert(dxp % 2 == 0, "dxp should be even");
    Assert(dyp % 2 == 0, "dyp should be even");

    auto pbwld = NewObj World;
    if (pbwld == pvNil || !pbwld->_FInit(dxp, dyp, _defaultRenderScale, fHalfX, fHalfY))
    {
        ReleasePpo(&pbwld);
        return pvNil;
    }

    AssertPo(pbwld, 0);
    return pbwld;
}

/***************************************************************************
    Initialize the World
***************************************************************************/
bool World::_FInit(long dxp, long dyp, long scale, bool fHalfX, bool fHalfY)
{
    AssertBaseThis(0);
    AssertIn(dxp, 1, ksuMax); // BPMP's width and height are ushorts
    AssertIn(dyp, 1, ksuMax);

    if (!_fBRenderInited)
    {
#ifdef WIN
        PreviousDiagHandler = BrDiagHandlerSet(&WorldDiagHandler);
#endif
        BrBegin();
        BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
        _fBRenderInited = fTrue;
    }

    if (dxp <= 0 || dyp <= 0 || dxp > 4096 || dyp > 4096)
        return false;
    mm_world_camera_init(&_camera);
    mm_world_render_init(&_render);
    auto s = _BufferServices();
    if (!mm_world_buffers_init(&_buffers, dxp, dyp, scale, fHalfX, fHalfY, &s))
        return false;
    BrZbSetRenderBoundsCallback(_ActorRendered);
    return true;
}

/***************************************************************************
    Destructor for World
***************************************************************************/
World::~World()
{
    AssertBaseThis(0);
    mm_world_buffers_dispose(&_buffers);
}
bool World::FSetHalfMode(bool x, bool y)
{
    return _FSetBufferMode(x, y, _buffers.scale);
}
bool World::FSetRenderScale(long scale)
{
    return _FSetBufferMode(FHalfX(), FHalfY(), scale);
}
bool World::FSetRenderResolution(long scale, bool reduced)
{
    AssertThis(0);
    if (scale != 1 && scale != 2 && scale != 4)
        return false;
    auto s = _BufferServices();
    return mm_world_buffers_resolution(&_buffers, scale, reduced, &s) != 0;
}
bool World::_FSetBufferMode(bool x, bool y, long scale)
{
    AssertThis(0);
    if (scale != 1 && scale != 2 && scale != 4)
        return false;
    auto s = _BufferServices();
    bool ok = mm_world_buffers_mode(&_buffers, x, y, scale, &s) != 0;
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Completely close BRender, freeing all data structures that BRender
    knows about.  This invalidates all MODLs and MTRLs in existence.
***************************************************************************/
void World::CloseBRender(void)
{
    if (_fBRenderInited)
    {
        BrZbEnd();
        BrEnd();
#ifdef WIN
        BrDiagHandlerSet(PreviousDiagHandler);
#endif
        _fBRenderInited = fFalse;
    }
}

bool World::FSetBackground(MMResourceCache *cache, ChunkTagOrType rgbType, ChunkNumber rgb, ChunkTagOrType zType,
                           ChunkNumber z)
{
    AssertThis(0);
    AssertVarMem(cache);
    auto s = _BufferServices();
    return mm_world_buffers_background(&_buffers, cache, rgbType, rgb, zType, z, &s) != 0;
}

MMBRenderBackgroundWorld World::BackgroundServices()
{
    AssertThis(0);
    return {
        this,
        [](void *p, br_actor *light) { static_cast<PWorld>(p)->AddActor(light); },
        [](void *p, MMResourceCache *cache, MMU32 rgb, MMU32 depth) -> int {
            return static_cast<PWorld>(p)->FSetBackground(cache, kctgMbmp, rgb, kctgZbmp, depth);
        },
        [](void *p, const MMBackgroundCameraData *camera) {
            Assert(camera->far_z > camera->near_z, "Yon must be further than hither");
            auto world = static_cast<PWorld>(p);
            const auto services = world->_CameraServices();
            mm_world_camera_set(&world->_camera, &camera->matrix, camera->near_z, camera->far_z, camera->fov, &services);
        },
        [](void *p) { static_cast<PWorld>(p)->MarkDirty(); }
    };
}

/***************************************************************************
    Change the camera matrix
***************************************************************************/
MMWorldCameraServices World::_CameraServices()
{
    MMWorldCameraServices s = {};
    s.context = this;
    s.publish = [](void *p, const MMWorldCamera *camera) {
        mm_brender_world_camera_pose(&static_cast<PWorld>(p)->_bactCamera, camera);
    };
    s.dirty = [](void *p) {
        auto w = static_cast<PWorld>(p);
        mm_world_buffers_dirty(&w->_buffers, 0);
    };
    return s;
}
void World::SetCamera(BMAT34 *pbmat34, BRS zrHither, BRS zrYon, BRA aFov)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    Assert(zrYon > zrHither, "Yon must be further than hither");

    MMActorMatrix matrix;
    std::memcpy(&matrix, pbmat34, sizeof(matrix));
    auto s = _CameraServices();
    mm_world_camera_set(&_camera, &matrix, zrHither, zrYon, aFov, &s);
}

/***************************************************************************
    Get the camera matrix
***************************************************************************/
void World::GetCamera(BMAT34 *pbmat34, BRS *pzrHither, BRS *pzrYon, BRA *paFov)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    AssertNilOrVarMem(pzrHither);
    AssertNilOrVarMem(pzrYon);
    AssertNilOrVarMem(paFov);

    MMActorMatrix matrix;
    MMI32 nearZ, farZ;
    unsigned short fov;
    mm_world_camera_get(&_camera, &matrix, &nearZ, &farZ, &fov);
    std::memcpy(pbmat34, &matrix, sizeof(matrix));
    if (pzrHither)
        *pzrHither = nearZ;
    if (pzrYon)
        *pzrYon = farZ;
    if (paFov)
        *paFov = fov;
}

/***************************************************************************
    Render the world.  First, notify all Bodys that we're about to render,
    so they can clear their _pregn's.  Then clean the RGB and Z working
    buffers, since they're probably dirty from the last render.  Update
    some regions, and render everything.
***************************************************************************/
void World::Render(MMMovieSession *movie, const MMMovieLiveFrame *position)
{
    AssertThis(0);
    WorldRenderContext context = {this};
    auto services = _RenderServices(&context, movie, position);
    mm_world_render(&_render, &_buffers, &services);
}

/***************************************************************************
    "Prerender" the world.  That is, render the world, then copy it into
    the background buffers
***************************************************************************/
void World::Prerender(MMMovieSession *movie, const MMMovieLiveFrame *position)
{
    AssertThis(0);
    WorldRenderContext context = {this};
    auto services = _RenderServices(&context, movie, position);
    mm_world_prerender(&_render, &_buffers, &services);
}

/***************************************************************************
    "Un-Prerender" the world.  That is, restore the background bitmaps to
    the way they were before prerendering any actors
***************************************************************************/
void World::Unprerender(void)
{
    AssertThis(0);
    AssertVarMem(_buffers.cache);
    const auto buffers = _BufferServices();
    mm_world_buffers_restore_background(&_buffers, &buffers);
}

/***************************************************************************
    Callback for when a BACT is rendered.  Need to union with dirty
    region.
***************************************************************************/
void World::_ActorRendered(PBACT pbact, PBMDL pbmdl, PBMTL pbmtl, br_uint_8 bStyle, br_matrix4 *pbmat4ModelToScreen,
                          br_int_32 bounds[4])
{
    AssertVarMem(pbact);

    auto pbwld = static_cast<PWorld>(mm_brender_world_owner(pbact));
    AssertPo(pbwld, 0);
    MMI32 input[4] = {bounds[0], bounds[1], bounds[2], bounds[3]}, output[4];
    if (!mm_world_rendered_bounds(pbwld->_buffers.scale, input, output))
        return;
    RC rc(output[0], output[1], output[2], output[3]);
    if (pvNil != pbwld->_pfnbactrend)
        pbwld->_pfnbactrend(pbact, &rc); // call client callback
}

/***************************************************************************
    Mark the region that has been rendered (and needs to be copied to the
    screen)
***************************************************************************/
void World::MarkRenderedRegn(PGraphicsObject pgob, long dxp, long dyp)
{
    AssertThis(0);
    AssertPo(pgob, 0);
    if (dxp < INT32_MIN || dxp > INT32_MAX || dyp < INT32_MIN || dyp > INT32_MAX)
        return;
    WorldRenderContext context = {this};
    context.gob = pgob;
    auto services = _RenderServices(&context);
    mm_world_render_mark(&_buffers, dxp, dyp, &services);
}

/***************************************************************************
    Draw the World's working RGB buffer into pgnv.  The movie engine should
    have called World::MarkRenderedRegn before calling this, so only
    dirty_screen's bits will be copied.
***************************************************************************/
void World::Draw(PGraphicsEnvironment pgnv, RC *prcClip, long dxp, long dyp)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);
    if (dxp < INT32_MIN || dxp > INT32_MAX || dyp < INT32_MIN || dyp > INT32_MAX)
        return;
    WorldRenderContext context = {this};
    context.destination = pgnv;
    auto services = _RenderServices(&context);
    mm_world_render_draw(&_buffers, dxp, dyp, &services);
}

/***************************************************************************
    Add an actor to the world
***************************************************************************/
void World::AddActor(BACT *pbact)
{
    AssertThis(0);
    AssertVarMem(pbact);

    BrActorAdd(&_bactWorld, pbact);
}

/***************************************************************************
    Visit the actors under the point (xp, yp).
***************************************************************************/
void World::IterateActorsInPt(MMBRenderActorHit *visit, void *context, long xp, long yp)
{
    AssertThis(0);

    if (xp < INT32_MIN || xp > INT32_MAX || yp < INT32_MIN || yp > INT32_MAX)
        return;
    MMRenderFrame frame;
    mm_render_frame_borrow(&frame, &_bactWorld, &_bactCamera, &_bpmpRGB, &_bpmpZ);
    mm_brender_world_visit(&_buffers, &frame, xp, yp, visit, context);
}

/***************************************************************************
   If an actor is under (xp, yp), function returns fTrue and **pbact is the
   actor. If no actor is under (xp, yp), function returns fFalse.
***************************************************************************/
bool World::FClickedActor(long xp, long yp, BACT **ppbact)
{
    AssertThis(0);
    AssertVarMem(ppbact);

    if (xp < INT32_MIN || xp > INT32_MAX || yp < INT32_MIN || yp > INT32_MAX)
        mm_world_pick_begin(&_camera);
    else
    {
        MMRenderFrame frame;
        mm_render_frame_borrow(&frame, &_bactWorld, &_bactCamera, &_bpmpRGB, &_bpmpZ);
        mm_brender_world_pick(&_camera, &_buffers, &frame, xp, yp);
    }
    if (_camera.picked)
    {
        *ppbact = static_cast<PBACT>(_camera.picked);
        return fTrue;
    }

    // nothing was clicked
    TrashVar(ppbact);
    return fFalse;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the World.
***************************************************************************/
void World::AssertValid(LegacyUlong grf)
{
    World_PAR::AssertValid(fobjAllocated);
    const auto working = mm_world_buffer_pixels(&_buffers.working);
    Assert(mm_world_pixels_valid(&working, _buffers.buffer_width, _buffers.buffer_height), "Invalid World working buffer");
    const auto background = mm_world_buffer_pixels(&_buffers.background);
    Assert(mm_world_pixels_valid(&background, _buffers.buffer_width, _buffers.buffer_height), "Invalid World background");
    AssertDepthBitmap(_buffers.depth_working);
    AssertDepthBitmap(_buffers.depth_background);
    AssertNilOrVarMem(_buffers.cache);
}

/***************************************************************************
    Mark memory used by the World
***************************************************************************/
void World::MarkMem(void)
{
    AssertThis(0);
    World_PAR::MarkMem();
    mm_resource_cache_mark(_buffers.cache);
    for (auto depth : {_buffers.depth_working, _buffers.depth_background})
        if (depth)
            mm_depth_owner_services()->mark_memory(depth);
    for (auto region : {&_buffers.dirty_working, &_buffers.dirty_screen})
    {
        MarkPv(region->bands);
        MarkPv(const_cast<void *>(mm_region_band_data(region->bands)));
    }
}

MMWorldExportServices World::_ExportServices(void *context)
{
    MMWorldExportServices s = {};
    s.context = context;
    s.buffers = _BufferServices();
    s.render = _RenderServices(context);
    s.palette = [](void *) -> void * { return GraphicsPort::PglclrGetPalette(); };
    s.release_palette = [](void *, void *palette) { static_cast<PDynamicArray>(palette)->Release(); };
    s.bounds_callback = [](void *, int enabled) {
        BrZbSetRenderBoundsCallback(enabled ? World::_ActorRendered : nullptr);
    };
    s.raster = mm_brender_world_raster;
    s.write = [](void *p, const MMWorldPixels *pixels, void *palette, MMI32 width, MMI32 height) -> int {
        // The shared BMP writer consumes four-byte-aligned indexed rows.
        if (pixels->row != ((width + 3) & ~3))
            return 0;
        return FWriteBitmap(static_cast<WorldRenderContext *>(p)->filename, pixels->pixels,
                            static_cast<PDynamicArray>(palette), width, height);
    };
    return s;
}
bool World::FWriteBmp(PFilename pfni)
{
    AssertPo(pfni, 0);
    WorldRenderContext context = {this};
    context.filename = pfni;
    auto services = _ExportServices(&context);
    return mm_world_export_current(&_buffers, &services) != 0;
}
bool World::FWriteBmpScaled(PFilename pfni, long scale)
{
    AssertThis(0);
    AssertPo(pfni, 0);
    AssertIn(scale, 1, 17);
    if (scale < 1 || scale > 16)
        return false;
    WorldRenderContext context = {this};
    context.filename = pfni;
    auto services = _ExportServices(&context);
    return mm_world_export_scaled(&_buffers, scale, &services) != 0;
}

#endif // DEBUG

} // end of namespace BRender
