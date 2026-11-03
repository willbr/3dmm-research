/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "soc_core.h"
#include <cstdio>
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *, LegacyLong, schar *, void *, LegacyLong)
{
    return false;
}

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    String backgroundPath;
    backgroundPath.SetSzs(argv[2]);
    Filename backgroundFilename;
    if (!backgroundFilename.FBuildFromPath(&backgroundPath))
        return 1;
    auto backgroundFile = ChunkyFile::PcflOpen(&backgroundFilename, fcflNil);
    ChildChunkIdentification cameraChild;
    DataBlock cameraBlock;
    CameraPosition camera;
    const bool cameraOk = backgroundFile && backgroundFile->FGetKidChidCtg(kctgBkgd, 0, 0, kctgCam, &cameraChild) &&
                          backgroundFile->FFind(cameraChild.cki.ctg, cameraChild.cki.cno, &cameraBlock) &&
                          cameraBlock.FUnpackData() && cameraBlock.FReadRgb(&camera, sizeof(camera), 0);
    ReleasePpo(&backgroundFile);
    if (!cameraOk || camera.bo != kboCur)
        return 1;
    String path;
    path.SetSzs(argv[1]);
    Filename filename;
    if (!filename.FBuildFromPath(&path))
        return 1;
    auto file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
    if (!file)
        return 1;
    SceneData scene;
    ChunkNumber sceneNumber;
    scene.firstFrame = 1;
    scene.lastFrame = 10;
    bool ok = scene.FCreate(file, &sceneNumber);
    auto frames = GeneralGroup::PggNew(sizeof(SceneEvent));
    auto starts = GeneralGroup::PggNew(sizeof(SceneEvent));
    if (!frames || !starts)
        ok = false;
    BrBegin();
    BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
    for (int actor = 0; ok && actor < 5; ++actor)
    {
        ChunkNumber actorNumber;
        DataBlock block;
        ActorChunkOnFile header = {};
        header.bo = kboCur;
        header.osk = koskCur;
        header.arid = actor;
        header.nfrmFirst = 1;
        header.nfrmLast = 10;
        if (actor == 4)
            header.nfrmFirst = header.nfrmLast = knfrmInvalid;
        header.tagTmpl.sid = 1;
        header.tagTmpl.ctg = kctgTmpl;
        header.tagTmpl.cno = actor == 0 ? 0x2010 : actor == 1 ? 0x1020 : 0x1010;
        header.dxyzFullRte = {BrsAdd(camera.apos.xrPlace, BR_SCALAR(actor * 12 - 18)), camera.apos.yrPlace,
                              camera.apos.zrPlace};
        if (actor == 3)
        {
            String text;
            text.SetSzs(PszLit("3D"));
            TAG font = {1, pvNil, kctgTdf, 0};
            auto templateText = ThreeDText::PtdtNew(&text, tdtsNormal, &font);
            ChunkNumber templateNumber;
            const bool written = templateText && templateText->FWrite(file, kctgTmpl, &templateNumber);
            ReleasePpo(&templateText);
            if (!written)
            {
                ok = false;
                break;
            }
            header.tagTmpl.sid = 0;
            header.tagTmpl.cno = templateNumber;
        }
        byte oldHeader[40];
        CopyPb(&header, oldHeader, 24);
        CopyPb(&header.tagTmpl, oldHeader + 24, sizeof(header.tagTmpl));
        const LegacyLong headerBytes = actor == 1 ? sizeof(oldHeader) : sizeof(header);
        if (!file->FAddChild(kctgScen, sceneNumber, actor, headerBytes, kctgActr, &actorNumber, &block) ||
            !block.FWriteRgb(actor == 1 ? static_cast<void *>(oldHeader) : static_cast<void *>(&header), headerBytes,
                             0))
        {
            ok = false;
            break;
        }
        if (actor == 3 && !file->FAdoptChild(kctgActr, actorNumber, kctgTmpl, header.tagTmpl.cno, 0))
        {
            ok = false;
            break;
        }
        ActorTrack track;
        if (!track.FCreate())
        {
            ok = false;
            break;
        }
        RouteDistancePoint points[] = {{{rZero, rZero, rZero}, BR_SCALAR(40)}, {{BR_SCALAR(40), rZero, rZero}, rZero}};
        if (!track.route->FInsert(0, &points[0]) || !track.route->FInsert(1, &points[1]))
        {
            ok = false;
            break;
        }
        ActorEvent::Base event = {ActorEvent::aetAdd, 1, {0, rZero, 0}};
        ActorEvent::Add addition = {};
        ActorEvent::Action action = {0, 0};
        BRS step = BR_SCALAR(2);
        ok = track.events->FInsert(0, sizeof(addition), &addition, &event);
        event.aet = ActorEvent::aetActn;
        ok = ok && track.events->FInsert(1, sizeof(action), &action, &event);
        event.aet = ActorEvent::aetStep;
        ok = ok && track.events->FInsert(2, sizeof(step), &step, &event);
        if (actor == 4)
        {
            track.route->Delete(0, track.route->IvMac());
            while (track.events->IvMac())
                track.events->Delete(0);
        }
        ChunkNumber child;
        ok = ok && file->FAddChild(kctgActr, actorNumber, 0, track.route->CbOnFile(), kctgPath, &child, &block) &&
             track.route->FWrite(&block);
        auto wire = ActorData::PggWriteEvents(track.events);
        ok = ok && wire && file->FAddChild(kctgActr, actorNumber, 0, wire->CbOnFile(), kctgGgae, &child, &block) &&
             wire->FWrite(&block);
        ReleasePpo(&wire);
        std::uint32_t slot = actor;
        SceneEvent start = {1, sevtAddActr};
        ok = ok && starts->FInsert(starts->IvMac(), sizeof(slot), &slot, &start);
    }
    TAGOnFile background = {1, 0, kctgBkgd, 0};
    SceneEvent event = {1, sevtSetBkgd};
    ok = ok && starts->FInsert(starts->IvMac(), sizeof(background), &background, &event);
    std::uint32_t nextCamera = 1;
    SceneEvent change = {5, sevtChngCamera};
    ok = ok && frames->FInsert(0, sizeof(nextCamera), &nextCamera, &change);
    ok = ok && SceneData::FWriteEvents(file, sceneNumber, frames, starts) && file->FSave(kctgMvie);
    // A matching scene containing only its background proves the composed
    // preview actually draws actor/prop/text geometry into the authored view.
    ChunkNumber emptyScene;
    ok = ok && scene.FCreate(file, &emptyScene);
    for (int i = 0; i < 5; ++i)
        starts->Delete(0);
    ok = ok && SceneData::FWriteEvents(file, emptyScene, frames, starts) && file->FSave(kctgMvie);
    ReleasePpo(&starts);
    ReleasePpo(&frames);
    ReleasePpo(&file);
    Material_MTRL::ReleaseShadeTable();
    BrZbEnd();
    BrEnd();
    return ok ? 0 : 1;
}
