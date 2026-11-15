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
#include "engine_read_stream.h"
#include <cstring>
ASSERTNAME

RTCLASS(Background)

const ChildChunkID kchidBds = 0;  // Background default sound
const ChildChunkID kchidGllt = 0; // DynamicArray of LITEs
const ChildChunkID kchidGlcr = 0; // Palette
const br_colour kbrcLight = BR_COLOUR_RGB(0xff, 0xff, 0xff);

/***************************************************************************
    Add the background's chunks (excluding camera views) to the tag list
***************************************************************************/
bool Background::FAddTagsToTagl(PTAG ptagBkgd, PTagList ptagl)
{
    AssertVarMem(ptagBkgd);
    AssertPo(ptagl, 0);

    if (!ptagl->FInsertTag(ptagBkgd, fFalse))
        return fFalse;
    if (!ptagl->FInsertChild(ptagBkgd, kchidBds, kctgBds))
        return fFalse;
    if (!ptagl->FInsertChild(ptagBkgd, kchidGllt, kctgGllt))
        return fFalse;
    if (!ptagl->FInsertChild(ptagBkgd, kchidGlcr, kctgColorTable))
        return fFalse;

    // Have to cache first camera view since scene switches to it
    // automatically
    if (!ptagl->FInsertChild(ptagBkgd, 0, kctgCam))
        return fFalse;
    return fTrue;
}

/***************************************************************************
    Cache the background's chunks (excluding camera views) to HD
***************************************************************************/
bool Background::FCacheToHD(PTAG ptagBkgd)
{
    AssertVarMem(ptagBkgd);

    TAG tagBds;
    TAG tagGllt;
    TAG tagGlcr;
    TAG tagCam;

    // Build the child tags
    if (!vptagm->FBuildChildTag(ptagBkgd, kchidBds, kctgBds, &tagBds))
        return fFalse;
    if (!vptagm->FBuildChildTag(ptagBkgd, kchidGllt, kctgGllt, &tagGllt))
        return fFalse;
    if (!vptagm->FBuildChildTag(ptagBkgd, kchidGlcr, kctgColorTable, &tagGlcr))
        return fFalse;
    if (!vptagm->FBuildChildTag(ptagBkgd, 0, kctgCam, &tagCam))
        return fFalse;

    // Cache the Background chunk
    if (!vptagm->FCacheTagToHD(ptagBkgd, fFalse))
        return fFalse;

    // Cache the child chunks
    if (!vptagm->FCacheTagToHD(&tagBds))
        return fFalse;
    if (!vptagm->FCacheTagToHD(&tagGllt))
        return fFalse;
    if (!vptagm->FCacheTagToHD(&tagGlcr))
        return fFalse;

    // Have to cache first camera view since scene switches to it
    // automatically
    if (!vptagm->FCacheTagToHD(&tagCam))
        return fFalse;

    return fTrue;
}

/***************************************************************************
    A PFNRPO to read a Background from a file
***************************************************************************/
bool Background::FReadBkgd(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    Background *pbkgd;

    *pcb = size(Background) + size(BACT) + size(BLIT); // estimate Background size
    if (pvNil == ppbaco)
        return fTrue;
    pbkgd = NewObj Background;
    if (pvNil == pbkgd || !pbkgd->_FInit(pcrf->Pcfl(), ctg, cno))
    {
        TrashVar(ppbaco);
        TrashVar(pcb);
        ReleasePpo(&pbkgd);
        return fFalse;
    }
    AssertPo(pbkgd, 0);
    *ppbaco = pbkgd;
    *pcb =
        size(Background) + LwMul(pbkgd->_cbactLight, size(BACT)) + LwMul(pbkgd->_cbactLight, size(BLIT)); // actual Background size
    return fTrue;
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

    DataBlock blck;
    BackgroundFile bkgdf;
    ChildChunkIdentification kid;
    PDynamicArray pgllite = pvNil;
    short bo;

    _camera.count = _Ccam(pcfl, ctg, cno); // compute # of views in this background
    _camera.current = ivNil;

    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData())
        goto LFail;
    if (blck.Cb() != size(BackgroundFile))
        goto LFail;
    if (!blck.FReadRgb(&bkgdf, size(BackgroundFile), 0))
        goto LFail;
    if (kboCur != bkgdf.bo)
        SwapBytesBom(&bkgdf, kbomBkgdf);
    Assert(kboCur == bkgdf.bo, "bad BackgroundFile");

    if (!pcfl->FGetName(ctg, cno, &_stn))
        goto LFail;

    // Get the default sound
    if (pcfl->FGetKidChidCtg(ctg, cno, kchidBds, kctgBds, &kid))
    {
        BackgroundDefaultSoundOnFile bdsOnFile;
        if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck) || !blck.FUnpackData())
            goto LFail;
        if (blck.Cb() != size(BackgroundDefaultSoundOnFile))
            goto LFail;
        if (!blck.FReadRgb(&bdsOnFile, size(BackgroundDefaultSoundOnFile), 0))
            goto LFail;
        if (kboCur != bdsOnFile.bo)
            SwapBytesBom(&bdsOnFile, kbomBds);
        Assert(kboCur == bdsOnFile.bo, "bad BackgroundDefaultSound");
        _bds.bo = bdsOnFile.bo;
        _bds.osk = bdsOnFile.osk;
        _bds.vlm = bdsOnFile.vlm;
        _bds.fLoop = bdsOnFile.fLoop;
        TagFromOnFile(&_bds.tagSnd, bdsOnFile.tagSnd);
    }
    else
    {
        _bds.tagSnd.sid = ksidInvalid;
    }

    // If there is a GLCR child, get it
    if (pcfl->FGetKidChidCtg(ctg, cno, kchidGlcr, kctgColorTable, &kid))
    {
        if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
            goto LFail;
        _pglclr = DynamicArray::PglRead(&blck, &bo);
        if (_pglclr != pvNil)
        {
            if (kboOther == bo)
                SwapBytesRglw(_pglclr->QvGet(0), _pglclr->IvMac());
        }
        _bIndexBase = bkgdf.bIndexBase;
    }
    else
        _pglclr = pvNil;

    // Read the DynamicArray of LITEs (GLLT)
    if (!pcfl->FGetKidChidCtg(ctg, cno, kchidGllt, kctgGllt, &kid))
        goto LFail;
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        goto LFail;
    pgllite = DynamicArray::PglRead(&blck, &bo);
    if (pvNil == pgllite)
        goto LFail;
    Assert(pgllite->CbEntry() == size(LightPosition), "bad pgllite...you may need to update bkgds.chk");
    AssertBomRglw(kbomLite, size(LightPosition));
    if (kboOther == bo)
    {
        SwapBytesRglw(pgllite->QvGet(0), LwMul(pgllite->IvMac(), size(LightPosition) / size(int32_t)));
    }
    _cbactLight = pgllite->IvMac();
    if (!FAllocPv((void **)&_prgbactLight, LwMul(_cbactLight, size(BACT)), fmemClear, mprNormal))
    {
        goto LFail;
    }
    if (!FAllocPv((void **)&_prgblitLight, LwMul(_cbactLight, size(BLIT)), fmemClear, mprNormal))
    {
        goto LFail;
    }
    _SetupLights(pgllite);
    ReleasePpo(&pgllite);
    return fTrue;
LFail:
    Warn("Error reading background");
    ReleasePpo(&pgllite);
    return fFalse;
}

/***************************************************************************
    Return the number of camera views in this scene.  CameraPosition chunks need to be
    contiguous CHIDs starting at ChildChunkID 0.
***************************************************************************/
long Background::_Ccam(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    ChildChunkIdentification kid;
    long ccam;

    for (ccam = 0; pcfl->FGetKidChidCtg(ctg, cno, ccam, kctgCam, &kid); ccam++)
    {
    }
#ifdef DEBUG
    // Make sure chids are consecutive
    long ckid;
    long ccamT = 0;
    for (ckid = 0; pcfl->FGetKid(ctg, cno, ckid, &kid); ckid++)
    {
        if (kid.cki.ctg == kctgCam)
            ccamT++;
    }
    Assert(ccamT == ccam, "cam chids are not consecutive!");
#endif
    return ccam;
}

/***************************************************************************
    Fill _prgbactLight and _prgblitLight using a DynamicArray of LITEs
***************************************************************************/
void Background::_SetupLights(PDynamicArray pgllite)
{
    AssertBaseThis(0);
    AssertPo(pgllite, 0);

    long ilite;
    LightPosition *qlite;
    BACT *pbact;
    BLIT *pblit;

    for (ilite = 0; ilite < _cbactLight; ilite++)
    {
        qlite = (LightPosition *)pgllite->QvGet(ilite);
        pbact = &_prgbactLight[ilite];
        pblit = &_prgblitLight[ilite];
        pblit->type = (byte)qlite->lt;
        pblit->colour = kbrcLight;
        pblit->attenuation_c = qlite->rIntensity;
        pbact->type = BR_ACTOR_LIGHT;
        pbact->type_data = pblit;
        pbact->t.type = BR_TRANSFORM_MATRIX34;
        pbact->t.t.mat = qlite->bmat34;
    }
}

/***************************************************************************
    Clean up and delete this background
***************************************************************************/
Background::~Background(void)
{
    AssertBaseThis(0);
    Assert(!_fLeaveLitesOn, "Shouldn't be freeing background now");
    if (pvNil != _prgbactLight && pvNil != _prgblitLight)
        TurnOffLights();
    FreePpv((void **)&_prgbactLight);
    FreePpv((void **)&_prgblitLight);
    ReleasePpo(&_pglclr);
    auto services = _CameraServices(nullptr);
    three_mm_background_camera_dispose(&_camera, &services);
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

    *piclrMin = _bIndexBase;
    if (pvNil == _pglclr) // no custom palette
    {
        *ppglclr = DynamicArray::PglNew(size(Color)); // "palette" with 0 entries
    }
    else
    {
        *ppglclr = _pglclr->PglDup();
    }
    return (pvNil != *ppglclr);
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

    long ilite;
    BACT *pbact;

    if (!_fLites)
    {
        for (ilite = 0; ilite < _cbactLight; ilite++)
        {
            pbact = &_prgbactLight[ilite];
            pbwld->AddActor(pbact);
            BrLightEnable(pbact);
        }

        _fLites = fTrue;
    }
}

/***************************************************************************
    Turn off lights in pbwld
***************************************************************************/
void Background::TurnOffLights(void)
{
    AssertThis(0);

    long ilite;
    BACT *pbact;

    if (!_fLites || _fLeaveLitesOn)
        return;
    for (ilite = 0; ilite < _cbactLight; ilite++)
    {
        pbact = &_prgbactLight[ilite];
        BrLightDisable(pbact);
        BrActorRemove(pbact);
    }
    _fLites = fFalse;
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
    DataBlock block;
};
} // namespace
ThreeMMBackgroundCameraServices Background::_CameraServices(void *host)
{
    ThreeMMBackgroundCameraServices s = {};
    s.context = host;
    s.lights_on = [](void *p) {
        auto h = static_cast<BackgroundCameraHost *>(p);
        h->background->TurnOnLights(h->world);
    };
    s.read = [](void *p, ThreeMMI32 camera, ThreeMMReadStream *out, ThreeMMU32 *number) {
        auto h = static_cast<BackgroundCameraHost *>(p);
        auto background = h->background;
        auto file = background->Pcrf()->Pcfl();
        ChildChunkIdentification child;
        if (!file->FGetKidChidCtg(background->Ctg(), background->Cno(), camera, kctgCam, &child) ||
            !file->FFind(child.cki.ctg, child.cki.cno, &h->block) || !h->block.FUnpackData())
            return 0;
        *number = child.cki.cno;
        *out = EngineReadStream::FromBlock(h->block);
        return 1;
    };
    s.child = [](void *p, ThreeMMU32 camera, int depth, ThreeMMU32 *number) {
        auto h = static_cast<BackgroundCameraHost *>(p);
        ChildChunkIdentification child;
        if (!h->background->Pcrf()->Pcfl()->FGetKidChidCtg(kctgCam, camera, 0, depth ? kctgZbmp : kctgMbmp, &child))
            return 0;
        *number = child.cki.cno;
        return 1;
    };
    s.background = [](void *p, ThreeMMU32 rgb, ThreeMMU32 depth) {
        auto h = static_cast<BackgroundCameraHost *>(p);
        return int(h->world->FSetBackground(h->background->Pcrf(), kctgMbmp, rgb, kctgZbmp, depth));
    };
    s.new_points = [](void *, ThreeMMI32 count) -> void * {
        auto points = DynamicArray::PglNew(sizeof(APOS), count);
        if (points)
            AssertDo(points->FSetIvMac(count), "Should never fail");
        return points;
    };
    s.release = [](void *, void *points) { static_cast<PDynamicArray>(points)->Release(); };
    s.lock = [](void *, void *points) { static_cast<PDynamicArray>(points)->Lock(); };
    s.unlock = [](void *, void *points) { static_cast<PDynamicArray>(points)->Unlock(); };
    s.put_point = [](void *, void *points, ThreeMMI32 index, const ThreeMMActorPoint *point) {
        APOS value;
        value.xrPlace = point->dxr;
        value.yrPlace = point->dyr;
        value.zrPlace = point->dzr;
        static_cast<PDynamicArray>(points)->Put(index, &value);
    };
    s.point = [](void *, void *points, ThreeMMI32 index, ThreeMMActorPoint *out) {
        APOS value;
        static_cast<PDynamicArray>(points)->Get(index, &value);
        *out = {value.xrPlace, value.yrPlace, value.zrPlace};
    };
    s.point_count = [](void *, void *points) -> ThreeMMI32 { return static_cast<PDynamicArray>(points)->IvMac(); };
    s.euler_yxy = [](void *, const ThreeMMActorMatrix *value, unsigned short *a, unsigned short *c) {
        BMAT34 matrix;
        std::memcpy(&matrix, value, sizeof(matrix));
        BREUL angles;
        angles.order = BR_EULER_YXY_R;
        BrMatrix34ToEuler(&angles, &matrix);
        *a = angles.a;
        *c = angles.c;
    };
    s.rotate_y = [](void *, ThreeMMActorMatrix *out, unsigned short yaw) {
        BMAT34 matrix;
        BrMatrix34RotateY(&matrix, yaw);
        std::memcpy(out, &matrix, sizeof(matrix));
    };
    s.translate = [](void *, ThreeMMActorMatrix *out, const ThreeMMActorPoint *point) {
        BMAT34 matrix;
        std::memcpy(&matrix, out, sizeof(matrix));
        BrMatrix34PostTranslate(&matrix, point->dxr, point->dyr, point->dzr);
        std::memcpy(out, &matrix, sizeof(matrix));
    };
    s.camera = [](void *p, const ThreeMMBackgroundCameraData *camera) {
        auto h = static_cast<BackgroundCameraHost *>(p);
        BMAT34 matrix;
        std::memcpy(&matrix, &camera->matrix, sizeof(matrix));
        h->world->SetCamera(&matrix, camera->near_z, camera->far_z, camera->fov);
    };
    s.dirty = [](void *p) { static_cast<BackgroundCameraHost *>(p)->world->MarkDirty(); };
    s.diagnostic = [](void *, int issue) {
        if (issue == THREE_MM_BACKGROUND_CAMERA_SHORT)
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
    auto services = _CameraServices(&host);
    return three_mm_background_camera_set(&_camera, icam, &services) != 0;
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
    ThreeMMActorPoint point;
    three_mm_background_place(&_camera, &point, &services);
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
    three_mm_background_reuse_place(&_camera);
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
    AssertIn(_cbactLight, 1, 100); // 100 is sanity check
    AssertIn(_camera.count, 1, 100);       // 100 is sanity check
    Assert(_camera.current == ivNil || (_camera.current >= 0 && _camera.current < _camera.count), "bad _camera.current");
    AssertPvCb(_prgbactLight, LwMul(_cbactLight, size(BACT)));
    AssertPvCb(_prgblitLight, LwMul(_cbactLight, size(BLIT)));
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
    MarkPv(_prgbactLight);
    MarkPv(_prgblitLight);
    MarkMemObj(_pglclr);
    MarkMemObj(static_cast<PDynamicArray>(_camera.points));
}
#endif // DEBUG
