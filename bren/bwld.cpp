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
static ThreeMMWorldMaterialServices WorldMaterialServices()
{
    ThreeMMWorldMaterialServices s = {};
    s.gouraud = BR_MATF_GOURAUD;
    s.count = [](void *) -> size_t { return BrMaterialCount(nullptr); };
    s.enumerate = [](void *, int (*visit)(void *, void *), void *argument) {
        WorldMaterialEnumeration e = {visit, argument};
        BrMaterialEnum(nullptr, WorldMaterialVisit, &e);
    };
    s.read = [](void *, void *p, ThreeMMWorldMaterial *out) {
        auto m = static_cast<br_material *>(p);
        out->object = m;
        out->flags = m->flags;
        out->texture = m->colour_map;
        out->base = m->index_base;
        out->range = m->index_range;
    };
    s.write = [](void *, const ThreeMMWorldMaterial *in) {
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
class RenderMaterialScope
{
    ThreeMMWorldMaterials scope;
    ThreeMMWorldMaterialServices services;
    bool ready;

  public:
    RenderMaterialScope() : services(WorldMaterialServices())
    {
        ready = three_mm_world_materials_begin(&scope, World::FlatShading(), World::NoTexture(), &services) != 0;
    }
    bool Ready() const
    {
        return ready;
    }
    ~RenderMaterialScope()
    {
        three_mm_world_materials_end(&scope, &services);
    }
};
struct WorldRenderContext
{
    PWorld world;
    RegionScanner scanner;
    PGraphicsEnvironment destination = nullptr;
    PGraphicsObject gob = nullptr;
};
static PWorld RenderWorld(void *p)
{
    return static_cast<WorldRenderContext *>(p)->world;
}
static RC RenderRect(const ThreeMMI32 r[4])
{
    return RC(r[0], r[1], r[2], r[3]);
}
static void RenderRectOut(const RC &r, ThreeMMI32 out[4])
{
    out[0] = r.xpLeft;
    out[1] = r.ypTop;
    out[2] = r.xpRight;
    out[3] = r.ypBottom;
}
ThreeMMWorldRenderServices World::_RenderServices(void *context)
{
    ThreeMMWorldRenderServices s = {};
    s.context = context;
    s.root = &_bactWorld;
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
        return kind == THREE_MM_WORLD_BEGIN ? w->_pfnbeginrend != nullptr : w->_pfngetrect != nullptr;
    };
    s.begin = [](void *p, void *actor) { RenderWorld(p)->_pfnbeginrend(static_cast<PBACT>(actor)); };
    s.rect = [](void *p, void *actor, ThreeMMI32 out[4]) {
        RC r;
        RenderWorld(p)->_pfngetrect(static_cast<PBACT>(actor), &r);
        RenderRectOut(r, out);
    };
    s.options = [](void *, int *wire, int *flat, int *noTexture) {
        *wire = World::RenderWireframe();
        *flat = World::FlatShading();
        *noTexture = World::NoTexture();
    };
    s.region_bounds = [](void *p, int kind, ThreeMMI32 out[4]) -> int {
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
    s.region_add = [](void *p, int kind, const ThreeMMI32 rect[4]) {
        auto r = RenderRect(rect);
        static_cast<PRegion>(RenderWorld(p)->_buffers.owners[kind])->FUnionRc(&r);
    };
    s.depth_clean = [](void *p, const ThreeMMI32 rect[4]) {
        auto w = RenderWorld(p);
        auto r = RenderRect(rect);
        w->_pzbmpBackground->Draw(static_cast<byte *>(w->_bpmpZ.pixels), w->_bpmpZ.row_bytes, w->_bpmpZ.height, 0, 0,
                                  &r, w->_pregnDirtyWorking);
    };
    s.lock = [](void *p, int kind, ThreeMMWorldPixels *out) -> int {
        auto port = static_cast<PGraphicsPort>(RenderWorld(p)->_buffers.owners[kind]);
        RC r;
        out->pixels = port->PrgbLockPixels(&r);
        out->row = port->CbRow();
        out->bytes = size_t(out->row) * r.Dyp();
        return out->pixels != nullptr;
    };
    s.unlock = [](void *p, int kind) { static_cast<PGraphicsPort>(RenderWorld(p)->_buffers.owners[kind])->Unlock(); };
    s.scan_init = [](void *p, const ThreeMMI32 rect[4]) {
        auto c = static_cast<WorldRenderContext *>(p);
        auto r = RenderRect(rect);
        c->scanner.Init(c->world->_pregnDirtyWorking, &r);
    };
    s.scan_span = [](void *p, ThreeMMI32 *left, ThreeMMI32 *right) -> int {
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
    s.render = [](void *p) {
        auto w = RenderWorld(p);
        BrZbSceneRender(&w->_bactWorld, &w->_bactCamera, &w->_bpmpRGB, &w->_bpmpZ);
    };
    s.publish = [](void *p, const ThreeMMWorldRender *r) { RenderWorld(p)->_fWorldChanged = r->changed != 0; };
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
    s.screen_scale = [](void *p, ThreeMMI32 nx, ThreeMMI32 dx, ThreeMMI32 ny, ThreeMMI32 dy) {
        RenderWorld(p)->_pregnDirtyScreen->Scale(nx, dx, ny, dy);
    };
    s.screen_offset = [](void *p, ThreeMMI32 x, ThreeMMI32 y) { RenderWorld(p)->_pregnDirtyScreen->Offset(x, y); };
    s.screen_mark = [](void *p) {
        auto c = static_cast<WorldRenderContext *>(p);
        vpappb->MarkRegn(c->world->_pregnDirtyScreen, c->gob);
    };
    s.draw = [](void *p, const ThreeMMI32 source[4], const ThreeMMI32 destination[4]) {
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

ThreeMMWorldBufferServices World::_BufferServices()
{
    ThreeMMWorldBufferServices s = {};
    s.context = this;
    s.rgb_type = BR_PMT_INDEX_8;
    s.depth_type = BR_PMT_DEPTH_16;
    s.create = [](void *, int kind, ThreeMMI32 width, ThreeMMI32 height) -> void * {
        RC r(0, 0, width, height);
        if (kind == THREE_MM_WORLD_DEPTH_BACKGROUND || kind == THREE_MM_WORLD_DEPTH_WORKING)
            return ZBMP::PzbmpNew(width, height);
        if (kind == THREE_MM_WORLD_DIRTY_WORKING || kind == THREE_MM_WORLD_DIRTY_SCREEN)
            return Region::PregnNew(kind == THREE_MM_WORLD_DIRTY_SCREEN ? nullptr : &r);
        return GraphicsPort::PgptNewOffscreen(&r, kcbitPixelRGB);
    };
    s.retain = [](void *, void *p) { static_cast<BASE *>(p)->AddRef(); };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.lock = [](void *, void *p) -> void * { return static_cast<PGraphicsPort>(p)->PrgbLockPixels(); };
    s.unlock = [](void *, void *p) { static_cast<PGraphicsPort>(p)->Unlock(); };
    s.depth_span = [](void *, void *p, ThreeMMWorldDepthSpan *out) {
        auto d = static_cast<PZBMP>(p);
        *out = {static_cast<byte *>(d->_depth.pixels), d->_depth.row_bytes, d->_depth.bytes};
    };
    s.publish = [](void *p, const ThreeMMWorldBuffers *b) {
        auto w = static_cast<PWorld>(p);
        w->_rcView.Set(0, 0, b->width, b->height);
        w->_rcBuffer.Set(0, 0, b->buffer_width, b->buffer_height);
        w->_fHalfX = b->half_x != 0;
        w->_fHalfY = b->half_y != 0;
        w->_renderScale = b->scale;
        w->_pzbmpBackground = static_cast<PZBMP>(b->owners[THREE_MM_WORLD_DEPTH_BACKGROUND]);
        w->_pzbmpWorking = static_cast<PZBMP>(b->owners[THREE_MM_WORLD_DEPTH_WORKING]);
        w->_pgptBackground = static_cast<PGraphicsPort>(b->owners[THREE_MM_WORLD_RGB_BACKGROUND]);
        w->_pgptWorking = static_cast<PGraphicsPort>(b->owners[THREE_MM_WORLD_RGB_WORKING]);
        w->_pgptStretch = static_cast<PGraphicsPort>(b->owners[THREE_MM_WORLD_RGB_STRETCH]);
        w->_pregnDirtyWorking = static_cast<PRegion>(b->owners[THREE_MM_WORLD_DIRTY_WORKING]);
        w->_pregnDirtyScreen = static_cast<PRegion>(b->owners[THREE_MM_WORLD_DIRTY_SCREEN]);
        w->_pcrf = static_cast<PChunkyResourceFile>(b->owners[THREE_MM_WORLD_CACHE]);
        w->_ctgRGB = b->rgb_type;
        w->_cnoRGB = b->rgb_number;
        w->_ctgZ = b->z_type;
        w->_cnoZ = b->z_number;
        auto view = [](BPMP &p, const ThreeMMWorldPixelView &v) {
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
    s.fetch = [](void *, void *p, int kind, ThreeMMU32 type, ThreeMMU32 number) -> void * {
        return static_cast<PChunkyResourceFile>(p)->PbacoFetch(
            type, number, kind == THREE_MM_WORLD_DEPTH_BACKGROUND ? ZBMP::FReadZbmp : MaskedBitmapMBMP::FReadMbmp);
    };
    s.priority = [](void *, void *p) { static_cast<PBaseCacheableObject>(p)->SetCrep(crepTossFirst); };
    s.draw_bitmap = [](void *, void *port, void *bitmap) {
        GraphicsEnvironment gnv(static_cast<PGraphicsPort>(port));
        gnv.DrawMbmp(static_cast<PMaskedBitmapMBMP>(bitmap), 0, 0);
    };
    s.copy_rgb = [](void *, void *source, void *destination, ThreeMMI32 sw, ThreeMMI32 sh, ThreeMMI32 dw,
                    ThreeMMI32 dh) {
        GraphicsEnvironment src(static_cast<PGraphicsPort>(source)), dst(static_cast<PGraphicsPort>(destination));
        RC sr(0, 0, sw, sh), dr(0, 0, dw, dh);
        dst.CopyPixels(&src, &sr, &dr);
    };
    s.draw_depth = [](void *, void *source, void *destination, ThreeMMI32 width, ThreeMMI32 height) {
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
    three_mm_world_camera_init(&_camera);
    three_mm_world_render_init(&_render);
    auto s = _BufferServices();
    if (!three_mm_world_buffers_init(&_buffers, dxp, dyp, _renderScale, fHalfX, fHalfY, &s))
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
    three_mm_world_buffers_dispose(&_buffers, &s);
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
    return three_mm_world_buffers_resolution(&_buffers, scale, reduced, &s) != 0;
}
bool World::_FSetBufferMode(bool x, bool y, long scale)
{
    AssertThis(0);
    if (scale != 1 && scale != 2 && scale != 4)
        return false;
    auto s = _BufferServices();
    bool ok = three_mm_world_buffers_mode(&_buffers, x, y, scale, &s) != 0;
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
    return three_mm_world_buffers_background(&_buffers, cache, rgbType, rgb, zType, z, &s) != 0;
}

/***************************************************************************
    Change the camera matrix
***************************************************************************/
ThreeMMWorldCameraServices World::_CameraServices()
{
    ThreeMMWorldCameraServices s = {};
    s.context = this;
    s.publish = [](void *p, const ThreeMMWorldCamera *camera) {
        auto w = static_cast<PWorld>(p);
        static_assert(sizeof(BMAT34) == sizeof(camera->matrix), "camera matrix layout");
        static_assert(sizeof(BRS) == sizeof(ThreeMMI32), "fixed scalar width");
        std::memcpy(&w->_bactCamera.t.t.mat, &camera->matrix, sizeof(camera->matrix));
        w->_bcam.hither_z = camera->near_z;
        w->_bcam.yon_z = camera->far_z;
        w->_bcam.field_of_view = camera->fov;
    };
    s.dirty = [](void *p) {
        auto w = static_cast<PWorld>(p);
        w->_pregnDirtyWorking->SetRc(&w->_rcBuffer);
    };
    s.pick = [](void *p, ThreeMMI32 x, ThreeMMI32 y) {
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

    ThreeMMActorMatrix matrix;
    std::memcpy(&matrix, pbmat34, sizeof(matrix));
    auto s = _CameraServices();
    three_mm_world_camera_set(&_camera, &matrix, zrHither, zrYon, aFov, &s);
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

    ThreeMMActorMatrix matrix;
    ThreeMMI32 nearZ, farZ;
    unsigned short fov;
    three_mm_world_camera_get(&_camera, &matrix, &nearZ, &farZ, &fov);
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
void World::Render(void)
{
    AssertThis(0);
    WorldRenderContext context = {this};
    auto services = _RenderServices(&context);
    three_mm_world_render(&_render, &_buffers, &services);
}

/***************************************************************************
    "Prerender" the world.  That is, render the world, then copy it into
    the background buffers
***************************************************************************/
void World::Prerender(void)
{
    AssertThis(0);
    WorldRenderContext context = {this};
    auto services = _RenderServices(&context);
    three_mm_world_prerender(&_render, &_buffers, &services);
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
    three_mm_world_unprerender(&services);
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
    three_mm_world_render_clean(&_buffers, &services);
}

/***************************************************************************
    Callback for when a BACT is rendered.  Need to union with dirty
    region.
***************************************************************************/
void World::_ActorRendered(PBACT pbact, PBMDL pbmdl, PBMTL pbmtl, br_uint_8 bStyle, br_matrix4 *pbmat4ModelToScreen,
                          br_int_32 bounds[4])
{
    AssertVarMem(pbact);

    auto pbwld = static_cast<PWorld>(three_mm_world_actor_owner(
        pbact, [](void *p) -> void * { return static_cast<PBACT>(p)->parent; },
        [](void *p) -> void * { return static_cast<PBACT>(p)->identifier; }));
    AssertPo(pbwld, 0);
    ThreeMMI32 input[4] = {bounds[0], bounds[1], bounds[2], bounds[3]}, output[4];
    if (!three_mm_world_rendered_bounds(pbwld->_buffers.scale, input, output))
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
    three_mm_world_render_mark(&_buffers, dxp, dyp, &services);
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
    three_mm_world_render_draw(&_buffers, dxp, dyp, &services);
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

    int result = three_mm_world_pick_visit(&pbwld->_camera, pbact, dzpNear);
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
    ThreeMMI32 x, y;
    if (three_mm_world_pick_point(&_buffers, _bpmpRGB.origin_x, _bpmpRGB.origin_y, xp, yp, &x, &y))
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
        three_mm_world_pick_begin(&_camera);
    else
        three_mm_world_pick(&_camera, &_buffers, _bpmpRGB.origin_x, _bpmpRGB.origin_y, xp, yp, &s);
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

/******************************************************************************
    FWriteBmp
        Writes the current rendered buffer out to the given file

    Arguments:
        PFilename pfni -- the name of the file

    Returns: fTrue if the file could be written successfully

************************************************************ PETED ***********/
bool World::FWriteBmp(PFilename pfni)
{
    AssertPo(pfni, 0);

    bool fRet;
    RC rc;

    fRet = FWriteBitmap(pfni, _pgptWorking->PrgbLockPixels(&rc), GraphicsPort::PglclrGetPalette(), _rcBuffer.Dxp(),
                        _rcBuffer.Dyp());
    _pgptWorking->Unlock();
    return fRet;
}

/******************************************************************************
    _FFillHiResBackground
        Fetch the current background MBMP+ZBMP at native (rcView) resolution
        and nearest-neighbor upscale into pgptDst / pzbmpDst, which must be
        sized scale*_rcView.Dxp() x scale*_rcView.Dyp().
        Used by FWriteBmpScaled.
******************************************************************************/
bool World::_FFillHiResBackground(PGraphicsPort pgptDst, PZBMP pzbmpDst, long scale)
{
    AssertThis(0);
    AssertPo(pgptDst, 0);
    AssertPo(pzbmpDst, 0);
    AssertIn(scale, 1, 17);

    if (pvNil == _pcrf)
        return fTrue; // no background loaded; leave dst with default fill (0xff Z, 0 RGB)

    PMaskedBitmapMBMP pmbmp = pvNil;
    PZBMP pzbmpSrc = pvNil;
    PGraphicsPort pgptFull = pvNil;
    bool fRet = fFalse;
    RC rcView = _rcView;
    RC rcHi(0, 0, LwMul(rcView.Dxp(), scale), LwMul(rcView.Dyp(), scale));

    pmbmp = (PMaskedBitmapMBMP)_pcrf->PbacoFetch(_ctgRGB, _cnoRGB, MaskedBitmapMBMP::FReadMbmp);
    if (pvNil == pmbmp)
        goto LFail;
    pzbmpSrc = (PZBMP)_pcrf->PbacoFetch(_ctgZ, _cnoZ, ZBMP::FReadZbmp);
    if (pvNil == pzbmpSrc)
        goto LFail;

    // Render the MBMP into a full-resolution intermediate, then stretch into the
    // hi-res destination via CopyPixels (which handles indexed-8 stretching).
    pgptFull = GraphicsPort::PgptNewOffscreen(&rcView, kcbitPixelRGB);
    if (pvNil == pgptFull)
        goto LFail;
    {
        GraphicsEnvironment gnvFull(pgptFull);
        GraphicsEnvironment gnvHi(pgptDst);
        gnvFull.DrawMbmp(pmbmp, 0, 0);
        gnvHi.CopyPixels(&gnvFull, &rcView, &rcHi);
    }

    // Nearest-neighbor upscale the Z buffer. Bilinear would create halfway Z
    // values that don't represent real surface depths and would cause artifacts
    // at silhouettes, so we replicate each Z value scale*scale times.
    {
        Assert(kcbPixelZ == 2, "Z upscale assumes 16-bit Z");
        long dxpSrc = rcView.Dxp();
        long dypSrc = rcView.Dyp();
        long cbRowSrc = pzbmpSrc->CbRow();
        long cbRowDst = pzbmpDst->CbRow();
        Assert(cbRowSrc == LwMul(dxpSrc, kcbPixelZ), "unexpected src Z stride");
        Assert(cbRowDst == LwMul(LwMul(dxpSrc, scale), kcbPixelZ), "unexpected dst Z stride");
        byte *pbSrcRow = pzbmpSrc->Prgb();
        byte *pbDstRow = pzbmpDst->Prgb();
        for (long ypSrc = 0; ypSrc < dypSrc; ypSrc++)
        {
            ushort *psuSrc = (ushort *)pbSrcRow;
            ushort *psuDst = (ushort *)pbDstRow;
            for (long xpSrc = 0; xpSrc < dxpSrc; xpSrc++)
            {
                ushort suZ = *psuSrc++;
                for (long i = 0; i < scale; i++)
                    *psuDst++ = suZ;
            }
            // Replicate the row scale-1 more times.
            byte *pbRowFirst = pbDstRow;
            for (long i = 1; i < scale; i++)
            {
                pbDstRow += cbRowDst;
                CopyPb(pbRowFirst, pbDstRow, cbRowDst);
            }
            pbDstRow += cbRowDst;
            pbSrcRow += cbRowSrc;
        }
    }

    fRet = fTrue;

LFail:
    ReleasePpo(&pmbmp);
    ReleasePpo(&pzbmpSrc);
    ReleasePpo(&pgptFull);
    return fRet;
}

/******************************************************************************
    FWriteBmpScaled
        Render the current scene into a temporary supersampled buffer
        (scale x scale times the size of _rcView), then write it as a BMP.
        Camera, actors, render style and palette match the live render; only
        the rasterization resolution differs. The live working buffers are
        untouched, so this can be called at any time.

        BRender's rasterizer is resolution-independent for a given camera
        (aspect = rcView.Dxp()/rcView.Dyp()), so feeding it scale-sized BPMPs
        produces a true higher-resolution render of foreground actors. The
        background bitmap chunk is at native resolution, so we fetch it and
        nearest-neighbor upscale it as both the seed RGB and the seed Z so
        actors composite correctly against it.

        Half-mode is ignored: the screenshot always uses _rcView dimensions
        as the basis (scale=4 yields 4x of the on-screen viewport, not 4x of
        the half-mode reduced buffer).
******************************************************************************/
bool World::FWriteBmpScaled(PFilename pfni, long scale)
{
    AssertThis(0);
    AssertPo(pfni, 0);
    AssertIn(scale, 1, 17);

    // Keep exports inside the renderer's viewport cap. PIZ2TP1024 names a
    // texture-size specialization, not the maximum output resolution.
    const long kdxpRenderMax = 4096;

    bool rendered = false;
    long scaleRender = scale;
    while (scaleRender > 1 &&
           (LwMul(_rcView.Dxp(), scaleRender) > kdxpRenderMax || LwMul(_rcView.Dyp(), scaleRender) > kdxpRenderMax))
    {
        scaleRender--;
    }

    bool fRet = fFalse;
    PGraphicsPort pgptRender = pvNil;
    PZBMP pzbmpRender = pvNil;
    long dxpRender = LwMul(_rcView.Dxp(), scaleRender);
    long dypRender = LwMul(_rcView.Dyp(), scaleRender);
    RC rcRender(0, 0, dxpRender, dypRender);
    // Zero-init: BRender reads base_x / base_y / flags / map / context that
    // the live engine leaves at zero (its class-member BPMPs are value-init).
    BPMP bpmpRGB = {};
    BPMP bpmpZ = {};
    RC rcLock;

    pgptRender = GraphicsPort::PgptNewOffscreen(&rcRender, kcbitPixelRGB);
    if (pvNil == pgptRender)
        goto LFail;
    pzbmpRender = ZBMP::PzbmpNew(dxpRender, dypRender);
    if (pvNil == pzbmpRender)
        goto LFail;

    if (!_FFillHiResBackground(pgptRender, pzbmpRender, scaleRender))
        goto LFail;

    // Match Render()'s wireframe override.
    {
        WorldRenderContext context = {this};
        auto services = _RenderServices(&context);
        three_mm_world_render_styles(&_bactWorld, _fRenderWireframe ? BR_RSTYLE_EDGES : BR_RSTYLE_FACES, &services);
    }

    bpmpRGB.type = BR_PMT_INDEX_8;
    bpmpRGB.row_bytes = (short)LwMul(dxpRender, kcbPixelRGB);
    bpmpRGB.width = (ushort)dxpRender;
    bpmpRGB.height = (ushort)dypRender;
    bpmpRGB.origin_x = (short)(dxpRender / 2);
    bpmpRGB.origin_y = (short)(dypRender / 2);
    bpmpRGB.pixels = pgptRender->PrgbLockPixels();

    bpmpZ.type = BR_PMT_DEPTH_16;
    bpmpZ.row_bytes = (short)LwMul(dxpRender, kcbPixelZ);
    bpmpZ.width = (ushort)dxpRender;
    bpmpZ.height = (ushort)dypRender;
    bpmpZ.origin_x = bpmpRGB.origin_x;
    bpmpZ.origin_y = bpmpRGB.origin_y;
    bpmpZ.pixels = pzbmpRender->Prgb();

    BrZbSetRenderBoundsCallback(nullptr);
    {
        RenderMaterialScope materials;
        rendered = materials.Ready();
        if (rendered)
            BrZbSceneRender(&_bactWorld, &_bactCamera, &bpmpRGB, &bpmpZ);
    }
    BrZbSetRenderBoundsCallback(_ActorRendered);
    GraphicsPort::Flush();
    pgptRender->Unlock();
    if (!rendered)
        goto LFail;

    {
        // PglclrGetPalette() allocates a fresh palette copy that the caller
        // owns -- FWriteBitmap doesn't release it. (FWriteBmp above leaks for
        // the same reason; left untouched here.)
        PDynamicArray pglclr = GraphicsPort::PglclrGetPalette();
        fRet = FWriteBitmap(pfni, pgptRender->PrgbLockPixels(&rcLock), pglclr, dxpRender, dypRender);
        pgptRender->Unlock();
        ReleasePpo(&pglclr);
    }

LFail:
    // Locks and the render-bounds callback are restored before reaching here,
    // including a failed temporary material allocation.
    ReleasePpo(&pgptRender);
    ReleasePpo(&pzbmpRender);
    return fRet;
}

#endif // DEBUG

} // end of namespace BRender
