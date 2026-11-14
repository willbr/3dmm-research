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
#include <stdlib.h>
#include <cstdint>
#include <cstring>
#include <vector>
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
    exit(10);
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

// Overrides exist only during a render. Cached/newly loaded materials and
// movie saves always retain their original textures, palette ranges and flags.
class RenderMaterialScope
{
    struct SavedMaterial
    {
        br_material *material;
        br_uint_32 flags;
        br_pixelmap *texture;
        br_uint_8 indexBase, indexRange;
    };
    std::vector<SavedMaterial> saved;
    static br_uint_32 BR_CALLBACK Apply(br_material *material, void *context)
    {
        const bool removeTexture = World::NoTexture() && material->colour_map;
        const bool flatten =
            World::FlatShading() && (!material->colour_map || removeTexture) && (material->flags & BR_MATF_GOURAUD);
        if (removeTexture || flatten)
        {
            auto &scope = *static_cast<RenderMaterialScope *>(context);
            scope.saved.push_back(
                {material, material->flags, material->colour_map, material->index_base, material->index_range});
            if (removeTexture)
            {
                material->colour_map = nullptr;
                // Texture lighting rows are not palette indices. Use the
                // retail default solid material's grey ramp (entries 15-30).
                material->index_base = 15;
                material->index_range = 15;
            }
            if (flatten)
                material->flags &= ~BR_MATF_GOURAUD;
            BrMaterialUpdate(material, BR_MATU_ALL);
        }
        return 0;
    }

  public:
    RenderMaterialScope()
    {
        if (World::FlatShading() || World::NoTexture())
        {
            saved.reserve(BrMaterialCount(pvNil));
            BrMaterialEnum(pvNil, Apply, this);
        }
    }
    ~RenderMaterialScope()
    {
        for (const auto &entry : saved)
        {
            entry.material->flags = entry.flags;
            entry.material->colour_map = entry.texture;
            entry.material->index_base = entry.indexBase;
            entry.material->index_range = entry.indexRange;
            BrMaterialUpdate(entry.material, BR_MATU_ALL);
        }
    }
};

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
            w->_fWorldChanged = true;
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
static void _SetModelRenderStyleRec(BACT *pbact, br_uint_8 style)
{
    BACT *pchild;
    for (pchild = pbact->children; pvNil != pchild; pchild = pchild->next)
    {
        // Don't touch hilite (selection) actors which use BR_RSTYLE_BOUNDING_EDGES
        // to draw a wireframe bounding box around selected bodies, or any other
        // bounding-style actor.
        if (pchild->type == BR_ACTOR_MODEL && pchild->render_style != BR_RSTYLE_BOUNDING_POINTS &&
            pchild->render_style != BR_RSTYLE_BOUNDING_EDGES && pchild->render_style != BR_RSTYLE_BOUNDING_FACES)
        {
            pchild->render_style = style;
        }
        _SetModelRenderStyleRec(pchild, style);
    }
}

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

    PBACT pbact;
    RC rc;

    if (!_fWorldChanged)
        return;

    // Note that we only call pfnbeginrend on immediate children of
    // the world, because that will hit all the Bodys in Socrates.
    if (pvNil != _pfnbeginrend)
    {
        for (pbact = _bactWorld.children; pvNil != pbact; pbact = pbact->next)
        {
            // Body roots are BR_ACTOR_NONE
            if (pbact->type == BR_ACTOR_NONE)
                _pfnbeginrend(pbact);
        }
    }

    _CleanWorkingBuffers();

    // Now the working buffer is clean, but we should mark everything that
    // we just cleaned in _CleanWorkingBuffers as dirty in the screen buffer
    // so that the background shows through where actors used to be.
    // In most cases, _pregnDirtyScreen is empty now, so it's tempting to
    // just SwapVars the two regions.  But if Render() was called but Draw()
    // never was (as when you press play then stop playing before the end of
    // the movie), _pregnDirtyScreen may have some stuff still to copy.  So
    // we have to merge (union) _pregnDirtyWorking into _pregnDirtyScreen,
    // then clear _pregnDirtyWorking.
    _pregnDirtyScreen->FUnion(_pregnDirtyWorking);
    _pregnDirtyWorking->SetRc(pvNil);

    // Apply user-selected render style override (wireframe).  Each BR_ACTOR_MODEL
    // body part has its own non-default render_style (BR_RSTYLE_FACES) which
    // overrides any inherited setting, so we walk the tree and force every model
    // actor to the desired style.
    _SetModelRenderStyleRec(&_bactWorld, _fRenderWireframe ? BR_RSTYLE_EDGES : BR_RSTYLE_FACES);

    // Render the scene.  This will add stuff to _pregnDirtyWorking
    {
        RenderMaterialScope materials;
        BrZbSceneRender(&_bactWorld, &_bactCamera, &_bpmpRGB, &_bpmpZ);
    }

    for (pbact = _bactWorld.children; pvNil != pbact; pbact = pbact->next)
    {
        // Body roots are BR_ACTOR_NONE
        if (pbact->type == BR_ACTOR_NONE && pvNil != _pfngetrect)
        {
            _pfngetrect(pbact, &rc);
            if (_renderScale > 1)
                rc.Map(&_rcView, &_rcBuffer);
            _pregnDirtyWorking->FUnionRc(&rc);
        }
    }

    // Everything dirty in working buffer is dirty on screen too
    _pregnDirtyScreen->FUnion(_pregnDirtyWorking);

    _fWorldChanged = fFalse;
}

/***************************************************************************
    "Prerender" the world.  That is, render the world, then copy it into
    the background buffers
***************************************************************************/
void World::Prerender(void)
{
    AssertThis(0);

    GraphicsEnvironment gnvBackground(_pgptBackground);
    GraphicsEnvironment gnvWorking(_pgptWorking);

    Render();

    _pzbmpWorking->Draw((byte *)_pzbmpBackground->Prgb(), _pzbmpBackground->CbRow(), _rcBuffer.Dyp(), 0, 0, &_rcBuffer,
                        pvNil);

    // Need to detach _pzbmpBackground from the ChunkyResourceFile so when we unprerender,
    // a fresh copy of the ZBMP is fetched
    _pzbmpBackground->Detach();

    gnvBackground.CopyPixels(&gnvWorking, &_rcBuffer, &_rcBuffer);

    // Need to ensure that the current contents of _pgptWorking (just the
    // prerenderable actors) go into _pgptBackground
    GraphicsPort::Flush();
}

/***************************************************************************
    "Un-Prerender" the world.  That is, restore the background bitmaps to
    the way they were before prerendering any actors
***************************************************************************/
void World::Unprerender(void)
{
    AssertThis(0);

    // Ignore error...you'll just get weird visual effects and an error
    // will be reported elsewhere
    FSetBackground(_pcrf, _ctgRGB, _cnoRGB, _ctgZ, _cnoZ);
}

/***************************************************************************
    Copy _pregnDirtyWorking from background Z and RGB buffers to working
    Z and RGB buffers.
***************************************************************************/
void World::_CleanWorkingBuffers(void)
{
    AssertThis(0);

    RegionScanner regsc;
    long yp;
    long xpLeft;
    RC rcRegnBounds;
    RC rcClippedRegnBounds;
    byte *pbSrc;
    byte *pbDst;
    long cbRowCopy;
    RC rc;
    long cbRowSrc, cbRowDst;

    if (_pregnDirtyWorking->FEmpty(&rcRegnBounds))
        return;
    if (!rcClippedRegnBounds.FIntersect(&rcRegnBounds, &_rcBuffer))
        return;

    // Clean the Z buffer
    _pzbmpBackground->Draw((byte *)_bpmpZ.pixels, _bpmpZ.row_bytes, _bpmpZ.height, 0, 0, &rcClippedRegnBounds,
                           _pregnDirtyWorking);

    // Clean the RGB buffer
    regsc.Init(_pregnDirtyWorking, &rcClippedRegnBounds);
    yp = rcClippedRegnBounds.ypTop;
    cbRowSrc = _pgptBackground->CbRow();
    pbSrc = _pgptBackground->PrgbLockPixels() + LwMul(yp, cbRowSrc) + rcClippedRegnBounds.xpLeft;
    cbRowDst = _pgptWorking->CbRow();
    pbDst = _pgptWorking->PrgbLockPixels() + LwMul(yp, cbRowDst) + rcClippedRegnBounds.xpLeft;
    for (; yp < rcClippedRegnBounds.ypBottom; yp++)
    {
        while (regsc.XpCur() < klwMax)
        {
            xpLeft = regsc.XpCur();
            cbRowCopy = regsc.XpFetch() - xpLeft;
            regsc.XpFetch();
            CopyPb(pbSrc + xpLeft, pbDst + xpLeft, cbRowCopy);
        }
        pbSrc += cbRowSrc;
        pbDst += cbRowDst;
        regsc.ScanNext(1);
    }
    _pgptWorking->Unlock();
    _pgptBackground->Unlock();
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

    if (_renderScale > 1)
        _pregnDirtyScreen->Scale(1, _renderScale, 1, _renderScale);
    else
        _pregnDirtyScreen->Scale((_fHalfX ? 2 : 1), 1, (_fHalfY ? 2 : 1), 1);
    _pregnDirtyScreen->Offset(dxp, dyp);
    vpappb->MarkRegn(_pregnDirtyScreen, pgob);
    _pregnDirtyScreen->SetRc(pvNil); // screen is clean
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

    RC rc;
    GraphicsEnvironment gnvTemp(_pgptWorking);

    rc.OffsetCopy(&_rcView, -dxp, -dyp);
    pgnv->CopyPixels(&gnvTemp, &_rcBuffer, &rc);
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
    _SetModelRenderStyleRec(&_bactWorld, _fRenderWireframe ? BR_RSTYLE_EDGES : BR_RSTYLE_FACES);

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
        BrZbSceneRender(&_bactWorld, &_bactCamera, &bpmpRGB, &bpmpZ);
    }
    BrZbSetRenderBoundsCallback(_ActorRendered);
    GraphicsPort::Flush();
    pgptRender->Unlock();

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
    // Lock/Unlock pairs are balanced on every success path. The only goto
    // LFail jumps happen before any Lock, so we just release here.
    ReleasePpo(&pgptRender);
    ReleasePpo(&pzbmpRender);
    return fRet;
}

#endif // DEBUG

} // end of namespace BRender
