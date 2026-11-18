/***************************************************************************

    bwld.cpp: BRender world class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    To improve performance, World can render into a reduced area, then
    enlarge the resulting image	at display time.  _fHalfX reduces the
    horizontal resolution by half, and _fHalfY reduces the vertical
    resolution by half.  Both modes can be used together to render 1/4 as
    many pixels.  _rcBuffer is the area being rendered into; _rcView is
    the area to copy _rcBuffer into (with stretching, if necessary).
    _pregnDirtyWorking and _pregnDirtyScreen are in _rcBuffer's coordinate
    system.  However, in MarkRenderedRegn, _pregnDirtyScreen is briefly
    enlarged to _rcView's coordinate system, since the gob will be drawn at
    full view resolution.

***************************************************************************/
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include "bren.h"

// REVIEW *****: _pgptStretch is completely unused...remove it!

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

const long kcbitPixelRGB = 8; // RGB buffers are 8 bits deep
const long kcbPixelRGB = 1;

const long kcbitPixelZ = 16; // Z buffers are 16 bits deep
const long kcbPixelZ = 2;

bool World::_fBRenderInited = fFalse;
bool World::_fRenderWireframe = fFalse;
bool World::_fFlatShading = fFalse;
bool World::_fNoTexture = fFalse;
long World::_defaultRenderScale = 1;

// Only SDK field access and registry enumeration live in this adapter.
struct WorldMaterialEnumeration
{
    int (*visit)(void *, void *);
    void *argument;
};
static br_uint_32 BR_CALLBACK WorldMaterialVisit(br_material *material, void *context)
{
    auto e = static_cast<WorldMaterialEnumeration *>(context);
    return e->visit(e->argument, material);
}
static MMWorldMaterialServices WorldMaterialServices()
{
    MMWorldMaterialServices s = {};
    s.gouraud = BR_MATF_GOURAUD;
    s.count = [](void *) -> size_t { return BrMaterialCount(nullptr); };
    s.enumerate = [](void *, int (*visit)(void *, void *), void *argument) {
        WorldMaterialEnumeration e = {visit, argument};
        BrMaterialEnum(nullptr, WorldMaterialVisit, &e);
    };
    s.read = [](void *, void *p, MMWorldMaterial *out) {
        auto m = static_cast<br_material *>(p);
        out->object = m;
        out->flags = m->flags;
        out->texture = m->colour_map;
        out->base = m->index_base;
        out->range = m->index_range;
    };
    s.write = [](void *, const MMWorldMaterial *in) {
        auto m = static_cast<br_material *>(in->object);
        m->flags = in->flags;
        m->colour_map = static_cast<br_pixelmap *>(in->texture);
        m->index_base = in->base;
        m->index_range = in->range;
        BrMaterialUpdate(m, BR_MATU_ALL);
    };
    s.allocate = [](void *, size_t bytes) -> void * { return std::malloc(bytes); };
    s.free = [](void *, void *p) { std::free(p); };
    return s;
}
struct WorldRenderContext
{
    PWorld world;
    RegionScanner scanner;
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
MMWorldRenderServices World::_RenderServices(void *context, MMMovieSession *movie)
{
    MMWorldRenderServices s = {};
    s.context = context;
    s.movie = movie;
    mm_render_frame_borrow(&s.frame, &_bactWorld, &_bactCamera, &_bpmpRGB, &_bpmpZ);
    static_assert(sizeof(s.camera.matrix) == sizeof(_camera.matrix), "camera matrix layout");
    std::memcpy(s.camera.matrix, &_camera.matrix, sizeof(s.camera.matrix));
    s.camera.near_z = _camera.near_z;
    s.camera.far_z = _camera.far_z;
    s.camera.fov = _camera.fov;
    s.camera.aspect = _bcam.aspect;
    if (_pfnreadbody && _pfnpreparebody)
    {
        s.body = [](void *p, void *actor, MMRenderBody *out) -> int {
            return RenderWorld(p)->_pfnreadbody(static_cast<PBACT>(actor), out);
        };
        s.prepare_body = [](void *p, const MMRenderBody *body) { RenderWorld(p)->_pfnpreparebody(body); };
        s.retain_body = [](void *, void *body) { static_cast<BASE *>(body)->AddRef(); };
        s.release_body = [](void *, void *body) { static_cast<BASE *>(body)->Release(); };
    }
    s.actor_none = BR_ACTOR_NONE;
    s.actor_model = BR_ACTOR_MODEL;
    s.bounding_points = BR_RSTYLE_BOUNDING_POINTS;
    s.bounding_edges = BR_RSTYLE_BOUNDING_EDGES;
    s.bounding_faces = BR_RSTYLE_BOUNDING_FACES;
    s.faces = BR_RSTYLE_FACES;
    s.edges = BR_RSTYLE_EDGES;
    s.child = [](void *, void *p) -> void * { return static_cast<PBACT>(p)->children; };
    s.next = [](void *, void *p) -> void * { return static_cast<PBACT>(p)->next; };
    s.type = [](void *, void *p) -> int { return static_cast<PBACT>(p)->type; };
    s.style = [](void *, void *p) -> int { return static_cast<PBACT>(p)->render_style; };
    s.set_style = [](void *, void *p, int style) { static_cast<PBACT>(p)->render_style = style; };
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
    s.region_bounds = [](void *p, int kind, MMI32 out[4]) -> int {
        RC r;
        auto region = static_cast<PRegion>(RenderWorld(p)->_buffers.owners[kind]);
        if (region->FEmpty(&r))
            return 0;
        RenderRectOut(r, out);
        return 1;
    };
    s.region_clear = [](void *p, int kind) {
        static_cast<PRegion>(RenderWorld(p)->_buffers.owners[kind])->SetRc(nullptr);
    };
    s.region_union = [](void *p, int dst, int src) {
        auto &b = RenderWorld(p)->_buffers;
        static_cast<PRegion>(b.owners[dst])->FUnion(static_cast<PRegion>(b.owners[src]));
    };
    s.region_add = [](void *p, int kind, const MMI32 rect[4]) {
        auto r = RenderRect(rect);
        static_cast<PRegion>(RenderWorld(p)->_buffers.owners[kind])->FUnionRc(&r);
    };
    s.depth_clean = [](void *p, const MMI32 rect[4]) {
        auto w = RenderWorld(p);
        auto r = RenderRect(rect);
        w->_pzbmpBackground->Draw(static_cast<byte *>(w->_bpmpZ.pixels), w->_bpmpZ.row_bytes, w->_bpmpZ.height, 0, 0,
                                  &r, w->_pregnDirtyWorking);
    };
    s.lock = [](void *p, int kind, MMWorldPixels *out) -> int {
        auto port = static_cast<PGraphicsPort>(RenderWorld(p)->_buffers.owners[kind]);
        RC r;
        out->pixels = port->PrgbLockPixels(&r);
        out->row = port->CbRow();
        out->bytes = size_t(out->row) * r.Dyp();
        return out->pixels != nullptr;
    };
    s.unlock = [](void *p, int kind) { static_cast<PGraphicsPort>(RenderWorld(p)->_buffers.owners[kind])->Unlock(); };
    s.scan_init = [](void *p, const MMI32 rect[4]) {
        auto c = static_cast<WorldRenderContext *>(p);
        auto r = RenderRect(rect);
        c->scanner.Init(c->world->_pregnDirtyWorking, &r);
    };
    s.scan_span = [](void *p, MMI32 *left, MMI32 *right) -> int {
        auto &scan = static_cast<WorldRenderContext *>(p)->scanner;
        if (scan.XpCur() >= klwMax)
            return 0;
        *left = scan.XpCur();
        *right = scan.XpFetch();
        scan.XpFetch();
        return 1;
    };
    s.scan_next = [](void *p) { static_cast<WorldRenderContext *>(p)->scanner.ScanNext(1); };
    s.scan_end = [](void *p) { static_cast<WorldRenderContext *>(p)->scanner.Free(); };
    s.render = [](void *p, const MMRenderFrame *frame) {
        auto world = RenderWorld(p);
        const auto &camera = frame->scene->camera;
        std::memcpy(&world->_bactCamera.t.t.mat, camera.matrix, sizeof(camera.matrix));
        world->_bcam.field_of_view = camera.fov;
        world->_bcam.hither_z = camera.near_z;
        world->_bcam.yon_z = camera.far_z;
        world->_bcam.aspect = camera.aspect;
        BrZbSceneRender(static_cast<PBACT>(frame->world), static_cast<PBACT>(frame->camera),
                        static_cast<PBPMP>(frame->colour), static_cast<PBPMP>(frame->depth));
    };
    s.publish = [](void *p, const MMWorldRender *r) { RenderWorld(p)->_fWorldChanged = r->changed != 0; };
    s.snapshot_depth = [](void *p) {
        auto w = RenderWorld(p);
        w->_pzbmpWorking->Draw(w->_pzbmpBackground->Prgb(), w->_pzbmpBackground->CbRow(), w->_rcBuffer.Dyp(), 0, 0,
                               &w->_rcBuffer, nullptr);
    };
    s.detach_depth = [](void *p) { RenderWorld(p)->_pzbmpBackground->Detach(); };
    s.snapshot_rgb = [](void *p) {
        auto w = RenderWorld(p);
        GraphicsEnvironment dst(w->_pgptBackground), src(w->_pgptWorking);
        dst.CopyPixels(&src, &w->_rcBuffer, &w->_rcBuffer);
    };
    s.flush = [](void *) { GraphicsPort::Flush(); };
    s.restore_background = [](void *p) {
        auto w = RenderWorld(p);
        w->FSetBackground(w->_pcrf, w->_ctgRGB, w->_cnoRGB, w->_ctgZ, w->_cnoZ);
    };
    s.screen_scale = [](void *p, MMI32 nx, MMI32 dx, MMI32 ny, MMI32 dy) {
        RenderWorld(p)->_pregnDirtyScreen->Scale(nx, dx, ny, dy);
    };
    s.screen_offset = [](void *p, MMI32 x, MMI32 y) { RenderWorld(p)->_pregnDirtyScreen->Offset(x, y); };
    s.screen_mark = [](void *p) {
        auto c = static_cast<WorldRenderContext *>(p);
        vpappb->MarkRegn(c->world->_pregnDirtyScreen, c->gob);
    };
    s.draw = [](void *p, const MMI32 source[4], const MMI32 destination[4]) {
        auto c = static_cast<WorldRenderContext *>(p);
        auto src = RenderRect(source), dst = RenderRect(destination);
        GraphicsEnvironment temp(c->world->_pgptWorking);
        c->destination->CopyPixels(&temp, &src, &dst);
    };
    s.materials = WorldMaterialServices();
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
    s.create = [](void *, int kind, MMI32 width, MMI32 height) -> void * {
        RC r(0, 0, width, height);
        if (kind == MM_WORLD_DEPTH_BACKGROUND || kind == MM_WORLD_DEPTH_WORKING)
            return ZBMP::PzbmpNew(width, height);
        if (kind == MM_WORLD_DIRTY_WORKING || kind == MM_WORLD_DIRTY_SCREEN)
            return Region::PregnNew(kind == MM_WORLD_DIRTY_SCREEN ? nullptr : &r);
        return GraphicsPort::PgptNewOffscreen(&r, kcbitPixelRGB);
    };
    s.retain = [](void *, void *p) { static_cast<BASE *>(p)->AddRef(); };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.lock = [](void *, void *p) -> void * { return static_cast<PGraphicsPort>(p)->PrgbLockPixels(); };
    s.unlock = [](void *, void *p) { static_cast<PGraphicsPort>(p)->Unlock(); };
    s.depth_span = [](void *, void *p, MMWorldDepthSpan *out) {
        auto d = static_cast<PZBMP>(p);
        *out = {static_cast<byte *>(d->_depth.pixels), d->_depth.row_bytes, d->_depth.bytes};
    };
    s.publish = [](void *p, const MMWorldBuffers *b) {
        auto w = static_cast<PWorld>(p);
        w->_rcView.Set(0, 0, b->width, b->height);
        w->_rcBuffer.Set(0, 0, b->buffer_width, b->buffer_height);
        w->_fHalfX = b->half_x != 0;
        w->_fHalfY = b->half_y != 0;
        w->_renderScale = b->scale;
        w->_pzbmpBackground = static_cast<PZBMP>(b->owners[MM_WORLD_DEPTH_BACKGROUND]);
        w->_pzbmpWorking = static_cast<PZBMP>(b->owners[MM_WORLD_DEPTH_WORKING]);
        w->_pgptBackground = static_cast<PGraphicsPort>(b->owners[MM_WORLD_RGB_BACKGROUND]);
        w->_pgptWorking = static_cast<PGraphicsPort>(b->owners[MM_WORLD_RGB_WORKING]);
        w->_pgptStretch = static_cast<PGraphicsPort>(b->owners[MM_WORLD_RGB_STRETCH]);
        w->_pregnDirtyWorking = static_cast<PRegion>(b->owners[MM_WORLD_DIRTY_WORKING]);
        w->_pregnDirtyScreen = static_cast<PRegion>(b->owners[MM_WORLD_DIRTY_SCREEN]);
        w->_pcrf = static_cast<PChunkyResourceFile>(b->owners[MM_WORLD_CACHE]);
        w->_ctgRGB = b->rgb_type;
        w->_cnoRGB = b->rgb_number;
        w->_ctgZ = b->z_type;
        w->_cnoZ = b->z_number;
        auto view = [](BPMP &p, const MMWorldPixelView &v) {
            p.type = static_cast<br_uint_8>(v.type);
            p.row_bytes = static_cast<short>(v.row);
            p.width = static_cast<ushort>(v.width);
            p.height = static_cast<ushort>(v.height);
            p.origin_x = static_cast<short>(v.x);
            p.origin_y = static_cast<short>(v.y);
            p.pixels = v.pixels;
        };
        view(w->_bpmpRGB, b->rgb);
        view(w->_bpmpZ, b->z);
    };
    s.setup_scene = [](void *p) {
        auto w = static_cast<PWorld>(p);
        w->_bactWorld.type = BR_ACTOR_NONE;
        w->_bactWorld.t.type = BR_TRANSFORM_MATRIX34;
        BrMatrix34Identity(&w->_bactWorld.t.t.mat);
        w->_bactWorld.identifier = reinterpret_cast<char *>(w);
        w->_bactCamera.type = BR_ACTOR_CAMERA;
        w->_bactCamera.t.type = BR_TRANSFORM_MATRIX34;
        w->_bactCamera.type_data = &w->_bcam;
        w->_bcam.type = BR_CAMERA_PERSPECTIVE;
        w->_bcam.aspect = BR_SCALAR(double(w->_rcView.Dxp()) / double(w->_rcView.Dyp()));
        BrActorAdd(&w->_bactWorld, &w->_bactCamera);
    };
    s.dirty = [](void *p, int working, int screen, int changed) {
        auto w = static_cast<PWorld>(p);
        if (working)
            w->_pregnDirtyWorking->SetRc(&w->_rcBuffer);
        if (screen)
            w->_pregnDirtyScreen->SetRc(&w->_rcBuffer);
        if (changed)
            w->MarkDirty();
    };
    s.fetch = [](void *, void *p, int kind, MMU32 type, MMU32 number) -> void * {
        return static_cast<PChunkyResourceFile>(p)->PbacoFetch(
            type, number, kind == MM_WORLD_DEPTH_BACKGROUND ? ZBMP::FReadZbmp : MaskedBitmapMBMP::FReadMbmp);
    };
    s.priority = [](void *, void *p) { static_cast<PBaseCacheableObject>(p)->SetCrep(crepTossFirst); };
    s.draw_bitmap = [](void *, void *port, void *bitmap) {
        GraphicsEnvironment gnv(static_cast<PGraphicsPort>(port));
        gnv.DrawMbmp(static_cast<PMaskedBitmapMBMP>(bitmap), 0, 0);
    };
    s.copy_rgb = [](void *, void *source, void *destination, MMI32 sw, MMI32 sh, MMI32 dw,
                    MMI32 dh) {
        GraphicsEnvironment src(static_cast<PGraphicsPort>(source)), dst(static_cast<PGraphicsPort>(destination));
        RC sr(0, 0, sw, sh), dr(0, 0, dw, dh);
        dst.CopyPixels(&src, &sr, &dr);
    };
    s.draw_depth = [](void *, void *source, void *destination, MMI32 width, MMI32 height) {
        auto dst = static_cast<PZBMP>(destination);
        RC r(0, 0, width, height);
        static_cast<PZBMP>(source)->Draw(dst->Prgb(), dst->CbRow(), height, 0, 0, &r);
    };
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

    PWorld pbwld;

    pbwld = NewObj World;
    if (pbwld)
        pbwld->_renderScale = _defaultRenderScale;

    if (pbwld == pvNil || !pbwld->_FInit(dxp, dyp, fHalfX, fHalfY))
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
bool World::_FInit(long dxp, long dyp, bool fHalfX, bool fHalfY)
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
    if (!mm_world_buffers_init(&_buffers, dxp, dyp, _renderScale, fHalfX, fHalfY, &s))
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
    auto s = _BufferServices();
    mm_world_buffers_dispose(&_buffers, &s);
}
bool World::FSetHalfMode(bool x, bool y)
{
    return _FSetBufferMode(x, y, _renderScale);
}
bool World::FSetRenderScale(long scale)
{
    return _FSetBufferMode(_fHalfX, _fHalfY, scale);
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

bool World::FSetBackground(PChunkyResourceFile cache, ChunkTagOrType rgbType, ChunkNumber rgb, ChunkTagOrType zType,
                           ChunkNumber z)
{
    AssertThis(0);
    AssertPo(cache, 0);
    auto s = _BufferServices();
    return mm_world_buffers_background(&_buffers, cache, rgbType, rgb, zType, z, &s) != 0;
}

/***************************************************************************
    Change the camera matrix
***************************************************************************/
MMWorldCameraServices World::_CameraServices()
{
    MMWorldCameraServices s = {};
    s.context = this;
    s.publish = [](void *p, const MMWorldCamera *camera) {
        auto w = static_cast<PWorld>(p);
        static_assert(sizeof(BMAT34) == sizeof(camera->matrix), "camera matrix layout");
        static_assert(sizeof(BRS) == sizeof(MMI32), "fixed scalar width");
        std::memcpy(&w->_bactCamera.t.t.mat, &camera->matrix, sizeof(camera->matrix));
        w->_bcam.hither_z = camera->near_z;
        w->_bcam.yon_z = camera->far_z;
        w->_bcam.field_of_view = camera->fov;
    };
    s.dirty = [](void *p) {
        auto w = static_cast<PWorld>(p);
        w->_pregnDirtyWorking->SetRc(&w->_rcBuffer);
    };
    s.pick = [](void *p, MMI32 x, MMI32 y) {
        auto w = static_cast<PWorld>(p);
        BrScenePick2D(&w->_bactWorld, &w->_bactCamera, &w->_bpmpRGB, x, y, World::_FFilter, w);
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
void World::Render(MMMovieSession *movie)
{
    AssertThis(0);
    WorldRenderContext context = {this};
    auto services = _RenderServices(&context, movie);
    mm_world_render(&_render, &_buffers, &services);
}

/***************************************************************************
    "Prerender" the world.  That is, render the world, then copy it into
    the background buffers
***************************************************************************/
void World::Prerender(MMMovieSession *movie)
{
    AssertThis(0);
    WorldRenderContext context = {this};
    auto services = _RenderServices(&context, movie);
    mm_world_prerender(&_render, &_buffers, &services);
}

/***************************************************************************
    "Un-Prerender" the world.  That is, restore the background bitmaps to
    the way they were before prerendering any actors
***************************************************************************/
void World::Unprerender(void)
{
    AssertThis(0);
    WorldRenderContext context = {this};
    auto services = _RenderServices(&context);
    mm_world_unprerender(&services);
}

/***************************************************************************
    Copy _pregnDirtyWorking from background Z and RGB buffers to working
    Z and RGB buffers.
***************************************************************************/
void World::_CleanWorkingBuffers(void)
{
    AssertThis(0);
    WorldRenderContext context = {this};
    auto services = _RenderServices(&context);
    mm_world_render_clean(&_buffers, &services);
}

/***************************************************************************
    Callback for when a BACT is rendered.  Need to union with dirty
    region.
***************************************************************************/
void World::_ActorRendered(PBACT pbact, PBMDL pbmdl, PBMTL pbmtl, br_uint_8 bStyle, br_matrix4 *pbmat4ModelToScreen,
                          br_int_32 bounds[4])
{
    AssertVarMem(pbact);

    auto pbwld = static_cast<PWorld>(mm_world_actor_owner(
        pbact, [](void *p) -> void * { return static_cast<PBACT>(p)->parent; },
        [](void *p) -> void * { return static_cast<PBACT>(p)->identifier; }));
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
    _pregnDirtyScreen's bits will be copied.
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
    Filter callback proc for FClickedActor().  Saves pbact if it's the
    closest one hit so far.
***************************************************************************/
int World::_FFilter(BACT *pbact, PBMDL pbmdl, PBMTL pbmtl, BVEC3 *pbvec3RayPos, BVEC3 *pbvec3RayDir, BRS dzpNear,
                   BRS dzpFar, void *pvData)
{
    AssertVarMem(pbact);
    AssertVarMem(pbvec3RayPos);
    AssertVarMem(pbvec3RayDir);

    PWorld pbwld = (PWorld)pvData;
    AssertPo(pbwld, 0);

    int result = mm_world_pick_visit(&pbwld->_camera, pbact, dzpNear);
    pbwld->_pbactClosestClicked = static_cast<PBACT>(pbwld->_camera.picked);
    pbwld->_dzpClosestClicked = pbwld->_camera.pick_depth;
    return result;
}

/***************************************************************************
    Call pfnCallback for each actor under the point (xp, yp)
***************************************************************************/
void World::IterateActorsInPt(br_pick2d_cbfn *pfnCallback, void *pvArg, long xp, long yp)
{
    AssertThis(0);

    if (xp < INT32_MIN || xp > INT32_MAX || yp < INT32_MIN || yp > INT32_MAX)
        return;
    MMI32 x, y;
    if (mm_world_pick_point(&_buffers, _bpmpRGB.origin_x, _bpmpRGB.origin_y, xp, yp, &x, &y))
        BrScenePick2D(&_bactWorld, &_bactCamera, &_bpmpRGB, x, y, pfnCallback, pvArg);
}

/***************************************************************************
   If an actor is under (xp, yp), function returns fTrue and **pbact is the
   actor. If no actor is under (xp, yp), function returns fFalse.
***************************************************************************/
bool World::FClickedActor(long xp, long yp, BACT **ppbact)
{
    AssertThis(0);
    AssertVarMem(ppbact);

    auto s = _CameraServices();
    if (xp < INT32_MIN || xp > INT32_MAX || yp < INT32_MIN || yp > INT32_MAX)
        mm_world_pick_begin(&_camera);
    else
        mm_world_pick(&_camera, &_buffers, _bpmpRGB.origin_x, _bpmpRGB.origin_y, xp, yp, &s);
    _pbactClosestClicked = static_cast<PBACT>(_camera.picked);
    _dzpClosestClicked = _camera.pick_depth;

    if (pvNil != _pbactClosestClicked)
    {
        *ppbact = _pbactClosestClicked;
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
    AssertPo(_pgptWorking, 0);
    AssertPo(_pgptBackground, 0);
    AssertPo(_pzbmpWorking, 0);
    AssertPo(_pzbmpBackground, 0);
    AssertPo(_pregnDirtyWorking, 0);
    AssertPo(_pregnDirtyScreen, 0);
    AssertNilOrPo(_pcrf, 0);
    if (_renderScale == 1 && !_fHalfX && _fHalfY)
        AssertPo(_pgptStretch, 0);
    else
        Assert(pvNil == _pgptStretch, "don't need _pgptStretch!");
}

/***************************************************************************
    Mark memory used by the World
***************************************************************************/
void World::MarkMem(void)
{
    AssertThis(0);
    World_PAR::MarkMem();
    MarkMemObj(_pgptWorking);
    MarkMemObj(_pgptBackground);
    MarkMemObj(_pzbmpWorking);
    MarkMemObj(_pzbmpBackground);
    MarkMemObj(_pregnDirtyWorking);
    MarkMemObj(_pregnDirtyScreen);
    MarkMemObj(_pcrf);
    MarkMemObj(_pgptStretch);
}

// SDK descriptor construction is a typed primitive; export sequencing and
// resource/lock/palette ownership live in the C controller.
static BPMP ExportPixelmap(const MMWorldPixelView &view)
{
    BPMP p = {};
    p.type = static_cast<br_uint_8>(view.type);
    p.row_bytes = static_cast<short>(view.row);
    p.width = static_cast<ushort>(view.width);
    p.height = static_cast<ushort>(view.height);
    p.origin_x = static_cast<short>(view.x);
    p.origin_y = static_cast<short>(view.y);
    p.pixels = view.pixels;
    return p;
}
MMWorldExportServices World::_ExportServices(void *context)
{
    MMWorldExportServices s = {};
    s.context = context;
    s.buffers = _BufferServices();
    s.render = _RenderServices(context);
    s.palette = [](void *) -> void * { return GraphicsPort::PglclrGetPalette(); };
    s.lock = [](void *, void *port, MMWorldPixels *out) -> int {
        auto p = static_cast<PGraphicsPort>(port);
        RC r;
        out->pixels = p->PrgbLockPixels(&r);
        out->row = p->CbRow();
        out->bytes = size_t(out->row) * r.Dyp();
        return out->pixels != nullptr;
    };
    s.bounds_callback = [](void *, int enabled) {
        BrZbSetRenderBoundsCallback(enabled ? World::_ActorRendered : nullptr);
    };
    s.raster = [](void *, const MMRenderFrame *frame, const MMWorldPixelView *rgb, const MMWorldPixelView *depth) {
        auto colour = ExportPixelmap(*rgb), z = ExportPixelmap(*depth);
        BrZbSceneRender(static_cast<PBACT>(frame->world), static_cast<PBACT>(frame->camera), &colour, &z);
    };
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
