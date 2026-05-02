/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    modl.cpp: Model class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "soc.h"
ASSERTNAME

RTCLASS(Model)

// Pin BRender struct sizes that are wire-format-critical for .CHK MODL chunks.
// br_vertex has no embedded pointer so its size matches the wire format on
// every architecture. br_face does -- on x64 the in-memory size grows by 4
// bytes -- which is why faces go through BrFaceOnFile at the I/O boundary.
static_assert(sizeof(br_vertex) == 32, "br_vertex wire size drift -- check /Zp4 packing");
#if defined(_WIN64) || defined(_M_X64)
static_assert(sizeof(br_face) == 36, "br_face x64 in-memory size drift");
#else
static_assert(sizeof(br_face) == 32, "br_face x86 in-memory size drift");
#endif

/***************************************************************************
    Marshal a BrFaceOnFile (wire-format, 36 bytes) into a runtime br_face
    (40 bytes on x64 because of the 8-byte br_material* slot). The material
    pointer is always nil-initialized -- the chunk doesn't carry a real
    pointer; .3MM material binding happens elsewhere.
***************************************************************************/
static void _BrFaceFromOnFile(br_face *pdst, const BrFaceOnFile *psrc)
{
    pdst->vertices[0] = psrc->vertices[0];
    pdst->vertices[1] = psrc->vertices[1];
    pdst->vertices[2] = psrc->vertices[2];
    pdst->edges[0] = psrc->edges[0];
    pdst->edges[1] = psrc->edges[1];
    pdst->edges[2] = psrc->edges[2];
    pdst->material = pvNil;
    pdst->smoothing = psrc->smoothing;
    pdst->flags = psrc->flags;
    pdst->_pad0 = psrc->_pad0;
    // br_fraction is `short` in the BASED_FIXED build; n[] are 2-byte fields.
    pdst->n.v[0] = (br_fraction)psrc->n[0];
    pdst->n.v[1] = (br_fraction)psrc->n[1];
    pdst->n.v[2] = (br_fraction)psrc->n[2];
    pdst->d = (br_scalar)psrc->d;
}

/***************************************************************************
    Pack a runtime br_face into the wire-format BrFaceOnFile. Drops the
    material* (stored as a 4-byte placeholder of 0).
***************************************************************************/
static void _BrFaceToOnFile(BrFaceOnFile *pdst, const br_face *psrc)
{
    pdst->vertices[0] = psrc->vertices[0];
    pdst->vertices[1] = psrc->vertices[1];
    pdst->vertices[2] = psrc->vertices[2];
    pdst->edges[0] = psrc->edges[0];
    pdst->edges[1] = psrc->edges[1];
    pdst->edges[2] = psrc->edges[2];
    pdst->material_slot = 0;
    pdst->smoothing = psrc->smoothing;
    pdst->flags = psrc->flags;
    pdst->_pad0 = psrc->_pad0;
    pdst->n[0] = (int16_t)psrc->n.v[0];
    pdst->n[1] = (int16_t)psrc->n.v[1];
    pdst->n[2] = (int16_t)psrc->n.v[2];
    pdst->d = (int32_t)psrc->d;
}

/***************************************************************************
    Read cfac BrFaceOnFile entries from pblck at byte offset ib, optionally
    byteswap each (mask kbomBrf describes the on-file layout), and unpack
    into the runtime br_face[] at prgbrf. cfac == 0 is a no-op success.
***************************************************************************/
static bool _FReadModelFaces(PDataBlock pblck, br_face *prgbrf, long cfac, long ib, bool fSwap)
{
    if (cfac <= 0)
        return fTrue;
    long cb = LwMul(cfac, size(BrFaceOnFile));
    BrFaceOnFile *prgof = pvNil;
    bool fOk = fFalse;
    if (!FAllocPv((void **)&prgof, cb, fmemNil, mprNormal))
        goto LDone;
    if (!pblck->FReadRgb(prgof, cb, ib))
        goto LDone;
    for (long ifac = 0; ifac < cfac; ifac++)
    {
        if (fSwap)
            SwapBytesBom(&prgof[ifac], kbomBrf);
        _BrFaceFromOnFile(&prgbrf[ifac], &prgof[ifac]);
    }
    fOk = fTrue;
LDone:
    FreePpv((void **)&prgof);
    return fOk;
}

/***************************************************************************
    Create a new PModel based on some vertices and faces.
***************************************************************************/
PModel Model::PmodlNew(long cbrv, BRV *prgbrv, long cbrf, BRF *prgbrf)
{
    AssertIn(cbrv, 0, ksuMax); // ushort in br_model
    AssertPvCb(prgbrv, LwMul(cbrv, size(BRV)));
    AssertIn(cbrf, 0, ksuMax); // ushort in br_model
    AssertPvCb(prgbrf, LwMul(cbrf, size(BRF)));

    PModel pmodl;
    char szIdentifier[size(PModel) + 1];

    pmodl = NewObj Model;
    if (pvNil == pmodl)
        goto LFail;
    ClearPb(szIdentifier, size(PModel) + 1);
    pmodl->_pbmdl = BrModelAllocate(szIdentifier, cbrv, cbrf);
    if (pvNil == pmodl->_pbmdl)
        goto LFail;
    CopyPb(&pmodl, pmodl->_pbmdl->identifier, size(PModel));
    CopyPb(prgbrv, pmodl->_pbmdl->vertices, LwMul(cbrv, size(BRV)));
    CopyPb(prgbrf, pmodl->_pbmdl->faces, LwMul(cbrf, size(BRF)));
    BrModelAdd(pmodl->_pbmdl);
    AssertPo(pmodl, 0);
    return pmodl;
LFail:
    ReleasePpo(&pmodl);
    return pvNil;
}

/***************************************************************************
    A PFNRPO to read a Model from a file
***************************************************************************/
bool Model::FReadModl(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    Model *pmodl;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;

    if (!pblck->FUnpackData())
        goto LFail;
    *pcb = pblck->Cb();

    pmodl = NewObj Model;
    if (pvNil == pmodl || !pmodl->_FInit(pblck))
    {
        ReleasePpo(&pmodl);
    LFail:
        TrashVar(ppbaco);
        TrashVar(pcb);
        return pvNil;
    }
    AssertPo(pmodl, 0);

    *ppbaco = pmodl;
    return fTrue;
}

/***************************************************************************
    Reads a Model from a DataBlock
***************************************************************************/
bool Model::_FInit(PDataBlock pblck)
{
    AssertBaseThis(0);
    AssertPo(pblck, 0);

    ModelOnFile modlf;
    long cbrgbrv;
    long cbrgbrfOnFile;
    long ibrv;
    BRV *pbrv;
    Model *pmodlThis = this;
    char szIdentifier[size(PModel) + 1];

    ClearPb(szIdentifier, size(PModel) + 1);
    if (!pblck->FUnpackData())
        return fFalse;
    if (pblck->Cb() < size(ModelOnFile))
        return fFalse;
    if (!pblck->FReadRgb(&modlf, size(ModelOnFile), 0))
        return fFalse;
    if (kboOther == modlf.bo)
        SwapBytesBom(&modlf, kbomModlf);
    Assert(kboCur == modlf.bo, "bad Model!");

    // Wire-format face entries are 36 bytes each (BrFaceOnFile); the runtime
    // br_face is 40 bytes on x64 due to the embedded br_material* slot. All
    // chunk-offset math uses the on-file size; the read helper expands each
    // entry into the runtime shape.
    cbrgbrv = LwMul(modlf.cver, size(BRV));
    cbrgbrfOnFile = LwMul(modlf.cfac, size(BrFaceOnFile));

    if (modlf.rRadius == rZero)
    {
        // unprepared model.  Gotta prepare it.

        _pbmdl = BrModelAllocate(szIdentifier, modlf.cver, modlf.cfac);
        if (pvNil == _pbmdl)
            return fFalse;
        CopyPb(&pmodlThis, _pbmdl->identifier, size(PModel));
        if (!pblck->FReadRgb(_pbmdl->vertices, cbrgbrv, size(ModelOnFile)))
            return fFalse;
        if (!_FReadModelFaces(pblck, _pbmdl->faces, modlf.cfac, size(ModelOnFile) + cbrgbrv,
                              kboOther == modlf.bo))
            return fFalse;

        BrModelAdd(_pbmdl);

        // REVIEW *****: uncomment and expand the following code to prelight models
        //		BVEC3 bvec3;
        //		if (!_FPrelight(1, &bvec3))
        //			return fFalse;
    }
    else
    {
        // pre-prepared model.
        _pbmdl = BrModelAllocate(szIdentifier, 0, 0);
        if (pvNil == _pbmdl)
            return fFalse;
        CopyPb(&pmodlThis, _pbmdl->identifier, size(PModel));

        _pbmdl->prepared_vertices =
            (BRV *)BrResAllocate(_pbmdl, LwMul(modlf.cver, size(BRV)), BR_MEMORY_PREPARED_VERTICES);
        if (pvNil == _pbmdl->prepared_vertices)
            return fFalse;
        _pbmdl->prepared_faces = (BRF *)BrResAllocate(_pbmdl, LwMul(modlf.cfac, size(BRF)), BR_MEMORY_PREPARED_FACES);
        if (pvNil == _pbmdl->prepared_faces)
            return fFalse;

        if (!pblck->FReadRgb(_pbmdl->prepared_vertices, cbrgbrv, size(ModelOnFile)))
        {
            return fFalse;
        }
        if (kboOther == modlf.bo)
        {
            for (ibrv = 0, pbrv = _pbmdl->prepared_vertices; ibrv < modlf.cver; ibrv++, pbrv++)
            {
                SwapBytesBom(pbrv, kbomBrv);
            }
        }
        if (!_FReadModelFaces(pblck, _pbmdl->prepared_faces, modlf.cfac,
                              size(ModelOnFile) + cbrgbrv, kboOther == modlf.bo))
        {
            return fFalse;
        }

        _pbmdl->flags = BR_MODF_PREPREPARED;
        _pbmdl->nprepared_vertices = (ushort)modlf.cver;
        _pbmdl->nprepared_faces = (ushort)modlf.cfac;

        // The following code assumes that there is no material data
        // in the models.  If there is material data, the code will have
        // to change to read vertex groups and face groups from file.
        _pbmdl->nvertex_groups = 1;
        _pbmdl->nface_groups = 1;
        _pbmdl->vertex_groups = (br_vertex_group *)BrResAllocate(_pbmdl, size(br_vertex_group), BR_MEMORY_GROUPS);
        if (pvNil == _pbmdl->vertex_groups)
            return fFalse;
        _pbmdl->vertex_groups->material = pvNil;
        _pbmdl->vertex_groups->vertices = _pbmdl->prepared_vertices;
        _pbmdl->vertex_groups->nvertices = _pbmdl->nprepared_vertices;
        _pbmdl->face_groups = (br_face_group *)BrResAllocate(_pbmdl, size(br_face_group), BR_MEMORY_GROUPS);
        if (pvNil == _pbmdl->face_groups)
            return fFalse;
        _pbmdl->face_groups->material = pvNil;
        _pbmdl->face_groups->faces = _pbmdl->prepared_faces;
        _pbmdl->face_groups->nfaces = _pbmdl->nprepared_faces;
        _pbmdl->radius = modlf.rRadius;
        _pbmdl->bounds = modlf.brb;
        _pbmdl->pivot = modlf.bvec3Pivot;
        BrModelAdd(_pbmdl);
    }
    return fTrue;
}

/***************************************************************************
    Reads a BRender model from a .DAT file
***************************************************************************/
PModel Model::PmodlReadFromDat(Filename *pfni)
{
    AssertPo(pfni, ffniFile);

    String stn;
    PModel pmodl;

    pmodl = NewObj Model;
    if (pvNil == pmodl)
        goto LFail;
    pfni->GetStnPath(&stn);
    pmodl->_pbmdl = BrModelLoad(stn.Psz());
    if (pvNil == pmodl->_pbmdl)
        goto LFail;
    pmodl->_pbmdl->flags |= BR_MODF_KEEP_ORIGINAL;
    BrModelPrepare(pmodl->_pbmdl, BR_MPREP_ALL);
    Assert(CchSz(pmodl->_pbmdl->identifier) >= size(void *), "no room for pmodl ptr");
    CopyPb(&pmodl, pmodl->_pbmdl->identifier, size(void *));
    AssertPo(pmodl, 0);
    return pmodl;
LFail:
    ReleasePpo(&pmodl);
    return pvNil;
}

/***************************************************************************
    Returns a pointer to the Model that owns this BMDL
***************************************************************************/
PModel Model::PmodlFromBmdl(PBMDL pbmdl)
{
    AssertVarMem(pbmdl);
    // The Model* is stashed sizeof(PModel) bytes wide via CopyPb (see PmodlNew
    // and _FInit). On x64 sizeof(PModel) is 8, so reading via (long*) would
    // truncate to the low 32 bits and yield a bad pointer.
    PModel pmodl = *(PModel *)pbmdl->identifier;
    AssertPo(pmodl, 0);
    return pmodl;
}

/***************************************************************************
    Destructor
***************************************************************************/
Model::~Model(void)
{
    AssertBaseThis(0);
    if (pvNil != _pbmdl)
    {
        BrModelRemove(_pbmdl);
        BrModelFree(_pbmdl);
    }
}

/***************************************************************************
    Writes a Model to a chunk
***************************************************************************/
bool Model::FWrite(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    AssertPo(pcfl, 0);

    long cb;
    long cbrgbrv;
    long cbrgbrfOnFile;
    ModelOnFile *pmodlf;

    // br_vertex has no embedded pointer so its size matches the wire format
    // on every architecture. br_face does, so faces are packed into the
    // 36-byte BrFaceOnFile shape -- the runtime br_face is 40 bytes on x64.
    cbrgbrv = LwMul(_pbmdl->nprepared_vertices, size(br_vertex));
    cbrgbrfOnFile = LwMul(_pbmdl->nprepared_faces, size(BrFaceOnFile));
    cb = size(ModelOnFile) + cbrgbrv + cbrgbrfOnFile;
    if (!FAllocPv((void **)&pmodlf, cb, fmemClear, mprNormal))
        goto LFail;
    pmodlf->bo = kboCur;
    pmodlf->osk = koskCur;
    pmodlf->cver = _pbmdl->nprepared_vertices;
    pmodlf->cfac = _pbmdl->nprepared_faces;
    pmodlf->rRadius = _pbmdl->radius;
    pmodlf->brb = _pbmdl->bounds;
    pmodlf->bvec3Pivot = _pbmdl->pivot;
    CopyPb(_pbmdl->prepared_vertices, PvAddBv(pmodlf, size(ModelOnFile)), cbrgbrv);
    {
        BrFaceOnFile *prgof = (BrFaceOnFile *)PvAddBv(pmodlf, size(ModelOnFile) + cbrgbrv);
        for (long ifac = 0; ifac < _pbmdl->nprepared_faces; ifac++)
            _BrFaceToOnFile(&prgof[ifac], &_pbmdl->prepared_faces[ifac]);
    }
    if (!pcfl->FPutPv(pmodlf, cb, ctg, cno))
        goto LFail;
    FreePpv((void **)&pmodlf);
    return fTrue;
LFail:
    Warn("model save failed.");
    FreePpv((void **)&pmodlf);
    return fFalse;
}

/***************************************************************************
    Adjust glyph for a ThreeDFont.  It is centered in X and Z, with Y at the
    baseline, and we do some voodoo to get "kerning" (really "variable
    interletter spacing") to work.
***************************************************************************/
void Model::AdjustTdfCharacter(void)
{
    AssertThis(0);

    BRS dxrModl = Dxr();
    BRS dxrSpacing = _pbmdl->bounds.min.v[0];
    BRS dxr = BrsHalf(dxrModl) + dxrSpacing;
    BRS dzrModl = Dzr();
    BRS dzr = BrsHalf(dzrModl);
    long cbrv = _pbmdl->nprepared_vertices;
    long ibrv;

    for (ibrv = 0; ibrv < cbrv; ibrv++)
    {
        _pbmdl->prepared_vertices[ibrv].p.v[0] -= dxr;
        _pbmdl->prepared_vertices[ibrv].p.v[2] -= dzr;
    }
    _pbmdl->bounds.min.v[0] -= dxr;
    _pbmdl->bounds.max.v[0] -= dxr;
    _pbmdl->pivot.v[0] -= dxr;
    _pbmdl->bounds.min.v[0] -= dxrSpacing;
    if (dxrSpacing < BR_SCALAR(-0.01))
        dxrSpacing = BR_SCALAR(0.5);
    _pbmdl->bounds.max.v[0] += dxrSpacing;

    _pbmdl->bounds.min.v[2] -= dzr;
    _pbmdl->bounds.max.v[2] -= dzr;
    _pbmdl->pivot.v[2] -= dzr;
}

/***************************************************************************
    Prelight a model
    REVIEW *****: make this code more general
***************************************************************************/
bool Model::_FPrelight(long cblit, BVEC3 *prgbvec3Light)
{
    AssertIn(cblit, 1, 10);
    AssertPvCb(prgbvec3Light, LwMul(cblit, size(BVEC3)));
    AssertBaseThis(0);

    PBACT pbactWorld;
    PBACT pbactCamera;
    PBPMP pbpmpRGB;
    PBPMP pbpmpZ;
    PBACT pbactLight;
    BLIT blit;
    PBMTL pbmtl;
    long iblit;
    BCAM bcam = {"bah",
                 BR_CAMERA_PERSPECTIVE,
                 BR_ANGLE_DEG(60.0), // REVIEW *****
                 BR_SCALAR(1.0),
                 BR_SCALAR(100.0),
                 BR_SCALAR(16.0 / 9.0),
                 544,
                 306};

    const br_colour kbrcHilite = BR_COLOUR_RGB(255, 255, 255);
    const byte kbOpaque = 0xff;
    const br_ufraction kbrufKaHilite = BR_UFRACTION(0.10);
    const br_ufraction kbrufKdHilite = BR_UFRACTION(0.60);
    const br_ufraction kbrufKsHilite = BR_UFRACTION(0.00);
    const BRS krPowerHilite = BR_SCALAR(50);
    const long kiclrHilite = 108; // palette index for hilite color

    ClearPb(&blit, size(BLIT));
    blit.colour = BR_COLOUR_RGB(0xff, 0xff, 0xff);
    blit.type = BR_LIGHT_DIRECT;
    blit.attenuation_c = BR_SCALAR(1.0);

    pbactWorld = BrActorAllocate(BR_ACTOR_NONE, pvNil);
    if (pvNil == pbactWorld)
        goto LFail;

    pbactCamera = BrActorAllocate(BR_ACTOR_CAMERA, &bcam);
    if (pvNil == pbactCamera)
        goto LFail;
    BrActorAdd(pbactWorld, pbactCamera);

    for (iblit = 0; iblit < cblit; iblit++)
    {
        pbactLight = BrActorAllocate(BR_ACTOR_LIGHT, pvNil);
        if (pvNil == pbactLight)
            goto LFail;
        pbactLight->type_data = &blit;
        BrActorAdd(pbactWorld, pbactLight);
        BrLightEnable(pbactLight);
    }

    pbpmpRGB = BrPixelmapAllocate(BR_PMT_INDEX_8, 544, 306, 0, 0);
    pbpmpZ = BrPixelmapAllocate(BR_PMT_DEPTH_16, 544, 306, 0, 0);

    pbmtl = BrMaterialAllocate("Prelighting material");
    if (pvNil == pbmtl)
        goto LFail;
    pbmtl->colour = kbrcHilite;
    pbmtl->ka = kbrufKaHilite;
    pbmtl->kd = kbrufKdHilite, pbmtl->ks = kbrufKsHilite;
    pbmtl->power = krPowerHilite;
    pbmtl->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD;
    pbmtl->index_base = kiclrHilite;
    pbmtl->index_range = 8;
    BrMaterialAdd(pbmtl);
    BrZbSceneRenderBegin(pbactWorld, pbactCamera, pbpmpRGB, pbpmpZ);
    BrSceneModelLight(_pbmdl, pbmtl, pbactWorld, pvNil);
    BrZbSceneRenderEnd();
    BrMaterialRemove(pbmtl);

    Assert(cblit == 1, "code needs to get smarter...");
    BrLightDisable(pbactLight);

    BrActorFree(pbactWorld);

    return fTrue;
LFail:
    BrActorFree(pbactWorld);
    return fFalse;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the Model.
***************************************************************************/
void Model::AssertValid(ulong grf)
{
    Model_PAR::AssertValid(fobjAllocated);
    AssertVarMem(_pbmdl);
    Assert(*(PModel *)_pbmdl->identifier == this, "Bad Model identifier");
}

/***************************************************************************
    Mark memory used by the Model
***************************************************************************/
void Model::MarkMem(void)
{
    AssertThis(0);

    Model_PAR::MarkMem();
}
#endif // DEBUG
