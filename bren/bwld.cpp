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

    _rcView.Set(0, 0, dxp, dyp);

    if (!_FInitBuffers(dxp, dyp, fHalfX, fHalfY))
        return fFalse;

    // Create the world and initial camera
    _bactWorld.type = BR_ACTOR_NONE;
    _bactWorld.t.type = BR_TRANSFORM_MATRIX34;
    BrMatrix34Identity(&_bactWorld.t.t.mat);
    _bactWorld.identifier = (char *)this;
    _bactCamera.type = BR_ACTOR_CAMERA;
    _bactCamera.t.type = BR_TRANSFORM_MATRIX34;
    _bactCamera.type_data = &_bcam;
    _bcam.type = BR_CAMERA_PERSPECTIVE;
    // Note that the aspect ratio of the view is specified rather than the
    // ratio of the buffer so that even when rendering into a reduced
    // buffer, the actors come out right when stretched to _rcView in Draw().
    _bcam.aspect = BR_SCALAR((double)_rcView.Dxp() / (double)_rcView.Dyp());
    BrActorAdd(&_bactWorld, &_bactCamera);

    // Set up dirty region stuff
    _pregnDirtyWorking = Region::PregnNew(&_rcBuffer);
    if (pvNil == _pregnDirtyWorking)
        return fFalse;
    _pregnDirtyScreen = Region::PregnNew(pvNil);
    if (pvNil == _pregnDirtyScreen)
        return fFalse;

    BrZbSetRenderBoundsCallback(_ActorRendered);

    return fTrue;
}

/***************************************************************************
    Initialize or reinitialize members that depend on the values of _fHalfX
    and _fHalfY.  This function gets called by _FInit, and again every time
    FSetHalfMode is called.
***************************************************************************/
bool World::_FInitBuffers(long dxp, long dyp, bool fHalfX, bool fHalfY)
{
    AssertBaseThis(0);
    AssertIn(dxp, 1, ksuMax); // BPMP's width and height are ushorts
    AssertIn(dyp, 1, ksuMax);
    if (dxp > 4096 / _renderScale || dyp > 4096 / _renderScale)
        return false;

    _fHalfX = fHalfX;
    _fHalfY = fHalfY;
    dxp *= _renderScale;
    dyp *= _renderScale;
    if (_renderScale == 1 && _fHalfX)
        dxp /= 2;
    if (_renderScale == 1 && _fHalfY)
        dyp /= 2;

    _rcBuffer.Set(0, 0, dxp, dyp);

    ReleasePpo(&_pzbmpBackground);
    _pzbmpBackground = ZBMP::PzbmpNew(dxp, dyp);
    if (pvNil == _pzbmpBackground)
        return fFalse;

    // Set up the working Z-buffer
    ReleasePpo(&_pzbmpWorking);
    _pzbmpWorking = ZBMP::PzbmpNew(dxp, dyp);
    if (pvNil == _pzbmpWorking)
        return fFalse;
    Assert(kcbitPixelZ == 16, "change _bpmpZ.type");
    _bpmpZ.type = BR_PMT_DEPTH_16;
    _bpmpZ.row_bytes = (short)LwMul(dxp, kcbPixelZ);
    _bpmpZ.width = (ushort)dxp;
    _bpmpZ.height = (ushort)dyp;
    _bpmpZ.origin_x = dxp / 2;
    _bpmpZ.origin_y = dyp / 2;
    _bpmpZ.pixels = _pzbmpWorking->Prgb();

    // Set up background RGB buffer
    ReleasePpo(&_pgptBackground);
    _pgptBackground = GraphicsPort::PgptNewOffscreen(&_rcBuffer, kcbitPixelRGB);
    if (pvNil == _pgptBackground)
        return fFalse;

    // Set up the working RGB buffer
    if (pvNil != _pgptWorking)
    {
        _pgptWorking->Unlock();
        ReleasePpo(&_pgptWorking);
    }
    _pgptWorking = GraphicsPort::PgptNewOffscreen(&_rcBuffer, kcbitPixelRGB);
    if (pvNil == _pgptWorking)
        return fFalse;
    Assert(kcbitPixelRGB == 8, "change _bpmpRGB.type");
    _bpmpRGB.type = BR_PMT_INDEX_8;
    _bpmpRGB.row_bytes = (short)LwMul(dxp, kcbPixelRGB);
    _bpmpRGB.width = (ushort)dxp;
    _bpmpRGB.height = (ushort)dyp;
    _bpmpRGB.origin_x = dxp / 2;
    _bpmpRGB.origin_y = dyp / 2;
    _bpmpRGB.pixels = _pgptWorking->PrgbLockPixels();

    // If in _fHalfY and not _fHalfX, allocated a _rcView-sized buffer
    // for faster blitting -- see Draw()
    ReleasePpo(&_pgptStretch);
    if (_renderScale == 1 && !_fHalfX && _fHalfY)
    {
        _pgptStretch = GraphicsPort::PgptNewOffscreen(&_rcView, kcbitPixelRGB);
        if (pvNil == _pgptStretch)
            return fFalse;
    }

    return fTrue;
}

/***************************************************************************
    Destructor for World
***************************************************************************/
World::~World(void)
{
    AssertBaseThis(0);

    if (pvNil != _pgptWorking)
    {
        _pgptWorking->Unlock();
        ReleasePpo(&_pgptWorking);
    }
    ReleasePpo(&_pgptStretch);
    ReleasePpo(&_pgptBackground);
    ReleasePpo(&_pzbmpWorking);
    ReleasePpo(&_pzbmpBackground);
    ReleasePpo(&_pregnDirtyWorking);
    ReleasePpo(&_pregnDirtyScreen);
    ReleasePpo(&_pcrf);
}

/***************************************************************************
    Change reduced rendering mode
***************************************************************************/
bool World::FSetHalfMode(bool fHalfX, bool fHalfY)
{
    return _FSetBufferMode(fHalfX, fHalfY, _renderScale);
}

bool World::FSetRenderScale(long scale)
{
    if (scale != 1 && scale != 2 && scale != 4)
        return false;
    return _FSetBufferMode(_fHalfX, _fHalfY, scale);
}

bool World::FSetRenderResolution(long scale, bool reduced)
{
    if ((scale != 1 && scale != 2 && scale != 4) || (reduced && scale != 1))
        return false;
    return _FSetBufferMode(reduced, reduced, scale);
}

bool World::_FSetBufferMode(bool fHalfX, bool fHalfY, long scale)
{
    AssertThis(0);

    if (FPure(_fHalfX) == FPure(fHalfX) && FPure(_fHalfY) == FPure(fHalfY) && scale == _renderScale)
        return fTrue;

    bool fHalfXSave = _fHalfX;
    bool fHalfYSave = _fHalfY;
    const long scaleSave = _renderScale;
    RC rcBufferSave = _rcBuffer;
    PGraphicsPort pgptWorkingSave = _pgptWorking;
    PGraphicsPort pgptStretchSave = _pgptStretch;
    PGraphicsPort pgptBackgroundSave = _pgptBackground;
    BPMP bpmpRGBSave = _bpmpRGB;
    PZBMP pzbmpWorkingSave = _pzbmpWorking;
    PZBMP pzbmpBackgroundSave = _pzbmpBackground;
    BPMP bpmpZSave = _bpmpZ;

    _pgptWorking = pvNil;
    _pgptStretch = pvNil;
    _pgptBackground = pvNil;
    _pzbmpWorking = pvNil;
    _pzbmpBackground = pvNil;

    _renderScale = scale;
    if (!_FInitBuffers(_rcView.Dxp(), _rcView.Dyp(), fHalfX, fHalfY))
        goto LFail;

    if (pvNil != _pcrf)
    {
        // Reload the background at the new resolution
        if (!FSetBackground(_pcrf, _ctgRGB, _cnoRGB, _ctgZ, _cnoZ))
            goto LFail;
    }

    if (pvNil != pgptWorkingSave)
    {
        pgptWorkingSave->Unlock();
        ReleasePpo(&pgptWorkingSave);
    }
    ReleasePpo(&pgptStretchSave);
    ReleasePpo(&pgptBackgroundSave);
    ReleasePpo(&pzbmpWorkingSave);
    ReleasePpo(&pzbmpBackgroundSave);

    _pregnDirtyWorking->SetRc(&_rcBuffer);
    _pregnDirtyScreen->SetRc(&_rcBuffer);
    _fWorldChanged = true;

    AssertThis(0);

    return fTrue;
LFail:
    // Get rid of newly allocated buffers
    if (pvNil != _pgptWorking)
    {
        _pgptWorking->Unlock();
        ReleasePpo(&_pgptWorking);
    }
    ReleasePpo(&_pgptStretch);
    ReleasePpo(&_pgptBackground);
    ReleasePpo(&_pzbmpWorking);
    ReleasePpo(&_pzbmpBackground);

    // restore everything
    _fHalfX = fHalfXSave;
    _fHalfY = fHalfYSave;
    _renderScale = scaleSave;
    _rcBuffer = rcBufferSave;
    _bpmpZ = bpmpZSave;
    _bpmpRGB = bpmpRGBSave;

    _pgptWorking = pgptWorkingSave;
    _pgptStretch = pgptStretchSave;
    _pgptBackground = pgptBackgroundSave;
    _pzbmpWorking = pzbmpWorkingSave;
    _pzbmpBackground = pzbmpBackgroundSave;

    AssertThis(0);
    return fFalse;
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

/***************************************************************************
    Copy pvSrc into pvDst, skipping every other short.  This is called by
    FSetBackground for each row in a ZBMP when _fHalfX is fTrue.
***************************************************************************/
inline void SqueezePb(void *pvSrc, void *pvDst, long cbSrc)
{
    AssertIn(cbSrc, 0, kcbMax);
    Assert(cbSrc % (LwMul(2, kcbPixelZ)) == 0, "cbSrc is not aligned");
    AssertPvCb(pvSrc, cbSrc);
    AssertPvCb(pvDst, cbSrc / 2);

    Assert(size(short) == kcbPixelZ, 0);
    short *pswSrc = (short *)pvSrc;
    short *pswDst = (short *)pvDst;

    while (cbSrc != 0)
    {
        *pswDst++ = *pswSrc++;
        pswSrc++;
        cbSrc -= LwMul(2, kcbPixelZ);
    }
}

/***************************************************************************
    Load bitmaps from the given chunks into _pgptBackground and
    _pzbmpBackground.
***************************************************************************/
bool World::_FScaleBackground(PMaskedBitmapMBMP bitmap, PZBMP depth, long scale)
{
    auto full = GraphicsPort::PgptNewOffscreen(&_rcView, kcbitPixelRGB);
    if (!full)
        return false;
    {
        GraphicsEnvironment source(full), destination(_pgptBackground);
        source.DrawMbmp(bitmap, 0, 0);
        destination.CopyPixels(&source, &_rcView, &_rcBuffer);
    }
    ReleasePpo(&full);

    // Preserve exact scene depths at background silhouettes: interpolate no Z.
    const auto width = _rcView.Dxp(), height = _rcView.Dyp();
    auto nativeDepth = ZBMP::PzbmpNew(width, height);
    if (!nativeDepth)
        return false;
    depth->Draw(nativeDepth->Prgb(), nativeDepth->CbRow(), height, 0, 0, &_rcView);
    const auto sourcePitch = nativeDepth->CbRow(), destinationPitch = _pzbmpBackground->CbRow();
    const auto source = static_cast<const byte *>(nativeDepth->Prgb());
    auto destination = static_cast<byte *>(_pzbmpBackground->Prgb());
    for (long y = 0; y < height * scale; ++y)
    {
        const auto input = reinterpret_cast<const ushort *>(source + (y / scale) * sourcePitch);
        auto output = reinterpret_cast<ushort *>(destination + y * destinationPitch);
        for (long x = 0; x < width * scale; ++x)
            output[x] = input[x / scale];
    }
    ReleasePpo(&nativeDepth);
    return true;
}

bool World::FSetBackground(PChunkyResourceFile pcrf, ChunkTagOrType ctgRGB, ChunkNumber cnoRGB, ChunkTagOrType ctgZ, ChunkNumber cnoZ)
{
    AssertThis(0);
    AssertPo(pcrf, 0);

    PMaskedBitmapMBMP pmbmpNew;
    PZBMP pzbmpNew;

    pmbmpNew = (PMaskedBitmapMBMP)pcrf->PbacoFetch(ctgRGB, cnoRGB, MaskedBitmapMBMP::FReadMbmp);
    if (pvNil == pmbmpNew)
        return fFalse;

    pzbmpNew = (PZBMP)pcrf->PbacoFetch(ctgZ, cnoZ, ZBMP::FReadZbmp);
    if (pvNil == pzbmpNew)
    {
        ReleasePpo(&pmbmpNew);
        return fFalse;
    }

    // It's nice to cache these bitmaps if we can, but they should be
    // tossed first when memory gets tight because they take up a lot
    // of space, they're pretty fast to reload, and they are reloaded
    // at a time when it's okay for a pause (between scenes/views).
    pmbmpNew->SetCrep(crepTossFirst);
    pzbmpNew->SetCrep(crepTossFirst);

    if (_renderScale > 1)
    {
        const bool filled = _FScaleBackground(pmbmpNew, pzbmpNew, _renderScale);
        ReleasePpo(&pmbmpNew);
        ReleasePpo(&pzbmpNew);
        if (!filled)
            return false;
    }
    else if (_fHalfX || _fHalfY)
    {
        // Need to squeeze pmbmpNew and pzbmpNew into _pgptBackground
        // and _pzbmpBackground.  For pmbmpNew, it is drawn into a
        // full-size buffer, then reduced with CopyPixels.  For the
        // ZBMP, it is necessary to reduce it in code here.
        PGraphicsPort pgptFull;
        long yp;
        byte *pbSrc;
        byte *pbDst;
        long cbRowSrc;
        long cbRowDst;
        pgptFull = GraphicsPort::PgptNewOffscreen(&_rcView, kcbitPixelRGB);
        if (pvNil == _pgptBackground)
        {
            ReleasePpo(&pmbmpNew);
            ReleasePpo(&pzbmpNew);
            return fFalse;
        }
        GraphicsEnvironment gnvFull(pgptFull);
        GraphicsEnvironment gnvHalf(_pgptBackground);

        gnvFull.DrawMbmp(pmbmpNew, 0, 0);
        ReleasePpo(&pmbmpNew);
        gnvHalf.CopyPixels(&gnvFull, &_rcView, &_rcBuffer);
        ReleasePpo(&pgptFull);

        pbSrc = pzbmpNew->Prgb();
        pbDst = _pzbmpBackground->Prgb();
        cbRowSrc = pzbmpNew->CbRow();
        cbRowDst = _pzbmpBackground->CbRow();
        Assert(cbRowSrc - cbRowDst == (_fHalfX ? cbRowDst : 0), "bad src/dest width ratio");
        for (yp = 0; yp < _rcBuffer.Dyp(); yp++)
        {
            if (_fHalfX)
                SqueezePb(pbSrc, pbDst, cbRowSrc);
            else
                CopyPb(pbSrc, pbDst, cbRowSrc);
            pbSrc += cbRowSrc;
            if (_fHalfY)
                pbSrc += cbRowSrc; // skip rows in source
            pbDst += cbRowDst;
        }
        ReleasePpo(&pzbmpNew);
    }
    else // not in half mode
    {
        GraphicsEnvironment gnv(_pgptBackground);
        gnv.DrawMbmp(pmbmpNew, 0, 0);
        ReleasePpo(&pmbmpNew);

        ReleasePpo(&_pzbmpBackground);
        _pzbmpBackground = pzbmpNew;
    }

    // entire working buffer is dirty because of background change
    _pregnDirtyWorking->SetRc(&_rcBuffer);
    _fWorldChanged = fTrue;

    // Keep a reference to the background, in case we change to/from
    // halfmode and need to reload it.
    pcrf->AddRef();
    ReleasePpo(&_pcrf);
    _pcrf = pcrf;
    _ctgRGB = ctgRGB;
    _cnoRGB = cnoRGB;
    _ctgZ = ctgZ;
    _cnoZ = cnoZ;

    return fTrue;
}

/***************************************************************************
    Change the camera matrix
***************************************************************************/
void World::SetCamera(BMAT34 *pbmat34, BRS zrHither, BRS zrYon, BRA aFov)
{
    AssertThis(0);
    AssertVarMem(pbmat34);
    Assert(zrYon > zrHither, "Yon must be further than hither");

    _bactCamera.t.t.mat = *pbmat34;
    _bcam.hither_z = zrHither;
    _bcam.yon_z = zrYon;
    _bcam.field_of_view = aFov;

    // entire working buffer is dirty because of camera change
    _pregnDirtyWorking->SetRc(&_rcBuffer);
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

    *pbmat34 = _bactCamera.t.t.mat;
    if (pvNil != pzrHither)
        *pzrHither = _bcam.hither_z;
    if (pvNil != pzrYon)
        *pzrYon = _bcam.yon_z;
    if (pvNil != paFov)
        *paFov = _bcam.field_of_view;
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

    PBACT pbactT = pbact;
    PWorld pbwld;
    RC rc(bounds[0], bounds[1], bounds[2] + 1, bounds[3] + 1);

    while (pbactT->parent != pvNil)
        pbactT = pbactT->parent;
    pbwld = (PWorld)pbactT->identifier;
    AssertPo(pbwld, 0);
    if (pbwld->_renderScale > 1)
    {
        rc.xpLeft /= pbwld->_renderScale;
        rc.ypTop /= pbwld->_renderScale;
        rc.xpRight = (rc.xpRight + pbwld->_renderScale - 1) / pbwld->_renderScale;
        rc.ypBottom = (rc.ypBottom + pbwld->_renderScale - 1) / pbwld->_renderScale;
    }
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

    if (dzpNear < pbwld->_dzpClosestClicked)
    {
        pbwld->_pbactClosestClicked = pbact;
        pbwld->_dzpClosestClicked = dzpNear;
    }

    return fFalse; // fFalse means keep searching
}

/***************************************************************************
    Call pfnCallback for each actor under the point (xp, yp)
***************************************************************************/
void World::IterateActorsInPt(br_pick2d_cbfn *pfnCallback, void *pvArg, long xp, long yp)
{
    AssertThis(0);

    // Convert to _rcBuffer coordinates:
    if (_renderScale > 1)
    {
        xp *= _renderScale;
        yp *= _renderScale;
    }
    else
    {
        if (_fHalfX)
            xp /= 2;
        if (_fHalfY)
            yp /= 2;
    }
    xp -= _bpmpRGB.origin_x;
    yp -= _bpmpRGB.origin_y;

    BrScenePick2D(&_bactWorld, &_bactCamera, &_bpmpRGB, xp, yp, pfnCallback, pvArg);
}

/***************************************************************************
   If an actor is under (xp, yp), function returns fTrue and **pbact is the
   actor. If no actor is under (xp, yp), function returns fFalse.
***************************************************************************/
bool World::FClickedActor(long xp, long yp, BACT **ppbact)
{
    AssertThis(0);
    AssertVarMem(ppbact);

    _pbactClosestClicked = pvNil;
    _dzpClosestClicked = BR_SCALAR_MAX;

    IterateActorsInPt(World::_FFilter, this, xp, yp);

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
