/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "soc_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "rich_text_data.h"
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    std::fprintf(stderr, "%s:%d: %s\n", file, int(line), message);
    std::exit(1);
}

static PChunkyFile Open(const char *path)
{
    String text;
    Filename filename;
    text.SetSzs(const_cast<char *>(path));
    return filename.FBuildFromPath(&text) ? ChunkyFile::PcflOpen(&filename, fcflNil) : pvNil;
}

static bool Texture(PChunkyFile file, int variant, ChunkNumber *material)
{
    byte *pixels = pvNil;
    if (!FAllocPv(reinterpret_cast<void **>(&pixels), 256, fmemClear, mprNormal))
        return false;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            pixels[y * 16 + x] = byte(32 + 32 * ((x / 4 + y / 4 + variant) % 4));
    auto map = TextureMap::PtmapNew(pixels, 16, 16);
    ChunkNumber texture;
    MaterialOnFile header = {kboCur, koskCur, 0xffffff, BR_UFRACTION(.4), BR_UFRACTION(.6), 0, 15, 15, BR_SCALAR(50)};
    const bool ok = map && map->FWrite(file, kctgTmap, &texture) &&
                    file->FAddPv(&header, sizeof(header), kctgMtrl, material) &&
                    file->FAdoptChild(kctgMtrl, *material, kctgTmap, texture, 0);
    ReleasePpo(&map);
    return ok;
}

static bool Costume(ActorTrack &track, int frame, int set, int cmid, PChunkyResourceFile resource, ChunkNumber material)
{
    ActorEvent::Base event = {ActorEvent::aetCost, frame, {0, BR_SCALAR((frame - 1) * 2), 0}};
    ActorEvent::Costume costume = {};
    costume.ibset = set;
    costume.cmid = cmid;
    costume.fCmtl = resource == pvNil ? tYes : tNo;
    costume.tag = {resource ? 0 : 2, resource, kctgMtrl, material};
    TagManager::DupTag(&costume.tag);
    if (track.events->FInsert(track.events->IvMac(), sizeof(costume), &costume, &event))
        return true;
    TagManager::CloseTag(&costume.tag);
    return false;
}

static bool BatchPauses(PGeneralGroup frames, LegacyLong insertion)
{
    const WaitReason reasons[] = {witUntilClick, witForTime, witUntilSnd};
    for (int i = 0; i < 3; ++i)
    {
        SceneEvent event = {2 + i, sevtPause};
        SceneEventPause pause = {reasons[i], 6000};
        if (!frames->FInsert(insertion + i, sizeof(pause), &pause, &event))
            return false;
    }
    return true;
}
static bool BatchCredits(PChunkyFile file, ChunkNumber *number)
{
    RichTextData text;
    text.fontName.SetSzs(PszLit("Arial"));
    text.defaults.dypFont = 16;
    text.defaults.dxpDef = 180;
    text.defaults.lwAcrBack = 0x00ffffff;
    text.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
    text.text.pfil = FileObject::PfilCreateTemp();
    char credits[4096];
    int count = 0;
    for (int line = 0; line < 24; ++line)
        count += std::snprintf(credits + count, sizeof(credits) - count, "Scrolling credits line %d\r", line + 1);
    text.text.cb = count;
    if (!text.properties || !text.text.pfil || !text.text.pfil->FSetFpMac(count) ||
        !text.text.FWriteRgb(credits, count, 0))
        return false;
    ChunkIdentification rich;
    TextBoxData box;
    box.firstFrame = -2;
    box.maxFrame = 31;
    box.story = false; // Scrolling box, rather than a stationary story box.
    box.bounds.Set(40, 24, 220, 72);
    return text.FWrite(file, &rich) && file->FAdd(0, kctgTbox, number) && box.FWrite(file, *number) &&
           file->FAdoptChild(kctgTbox, *number, rich.ctg, rich.cno, 0);
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5 &&
        !(argc == 6 && (std::strcmp(argv[5], "--batch") == 0 || std::strcmp(argv[5], "--dissolve") == 0)))
    {
        std::fprintf(stderr,
                     "Usage: preview-scene-fixture OUT BACKGROUNDS MAIN_CHK [TEMPLATES [--batch|--dissolve]]\n");
        return 2;
    }
    const bool movie = argc >= 5;
    const bool batch = argc == 6 && std::strcmp(argv[5], "--batch") == 0;
    const bool dissolve = argc == 6 && std::strcmp(argv[5], "--dissolve") == 0;
    const int source = movie ? 2 : 1;
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
    scene.firstFrame = batch ? -2 : 1;
    scene.lastFrame = movie ? 30 : 10;
    bool ok = scene.FCreate(file, &sceneNumber);
    auto frames = GeneralGroup::PggNew(sizeof(SceneEvent));
    auto starts = GeneralGroup::PggNew(sizeof(SceneEvent));
    if (!frames || !starts)
        ok = false;
    BrBegin();
    BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
    PChunkyFile shadeFile = Open(argv[3]);
    ok = ok && shadeFile && Material_MTRL::FSetShadeTable(shadeFile, kctgTmap, 0);
    PChunkyFile templateFile = movie ? Open(argv[4]) : pvNil;
    PChunkyResourceFile templateResources = templateFile ? ChunkyResourceFile::PcrfNew(templateFile, 0) : pvNil;
    PChunkyResourceFile document = movie ? ChunkyResourceFile::PcrfNew(file, 0) : pvNil;
    PTemplate animated = pvNil;
    LegacyLong animation = 0, costumeSet = -1, originalCostume = 0, alternateCostume = 0;
    ChunkNumber textures[2] = {};
    if (movie)
    {
        ok = ok && templateResources && document;
        if (ok)
            animated = (PTemplate)templateResources->PbacoFetch(kctgTmpl, 0x2010, Template::FReadTmpl);
        ok = ok && animated;
        bool found = false;
        for (LegacyLong action = 0; ok && action < animated->Cactn(); ++action)
        {
            LegacyLong cells = 0;
            if (animated->FGetCcelActn(action, &cells) && cells >= 6)
            {
                animation = action;
                found = true;
                break;
            }
        }
        auto body = animated ? animated->PbodyCreate() : pvNil;
        for (LegacyLong set = 0; body && set < body->Cbset(); ++set)
            if (!animated->FBsetIsAccessory(set) && animated->CcmidOfBset(set) > 1)
            {
                costumeSet = set;
                originalCostume = animated->CmidOfBset(set, 0);
                alternateCostume = animated->CmidOfBset(set, 1);
                break;
            }
        ReleasePpo(&body);
        ok = ok && found && costumeSet >= 0 && Texture(file, 0, &textures[0]) && Texture(file, 1, &textures[1]);
        std::printf("Feature fixture: action=%d costume_set=%d costumes=%d/%d\n", int(animation), int(costumeSet),
                    int(originalCostume), int(alternateCostume));
    }
    ChunkNumber actors[5] = {};
    auto roll = movie ? StringTable_GST::PgstNew(sizeof(RollCallActorEntryOnFile)) : pvNil;
    const int count = movie ? 4 : 5;
    for (int actor = 0; ok && actor < count; ++actor)
    {
        ChunkNumber actorNumber;
        DataBlock block;
        ActorChunkOnFile header = {};
        header.bo = kboCur;
        header.osk = koskCur;
        header.arid = actor;
        header.nfrmFirst = 1;
        header.nfrmLast = scene.lastFrame;
        if (actor == 4)
            header.nfrmFirst = header.nfrmLast = knfrmInvalid;
        header.tagTmpl.sid = source;
        header.tagTmpl.ctg = kctgTmpl;
        header.tagTmpl.cno = actor == 0 ? 0x2010 : actor == 1 ? 0x1020 : 0x1010;
        header.dxyzFullRte = {BrsAdd(camera.apos.xrPlace, BR_SCALAR(actor * 12 - 18)), camera.apos.yrPlace,
                              camera.apos.zrPlace};
        if (actor == 3)
        {
            String text;
            text.SetSzs(PszLit("3D"));
            TAG font = {source, pvNil, kctgTdf, 0};
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
        const LegacyLong headerBytes = actor == 1 && !movie ? sizeof(oldHeader) : sizeof(header);
        if (!file->FAddChild(kctgScen, sceneNumber, actor, headerBytes, kctgActr, &actorNumber, &block) ||
            !block.FWriteRgb(actor == 1 && !movie ? static_cast<void *>(oldHeader) : static_cast<void *>(&header),
                             headerBytes, 0))
        {
            ok = false;
            break;
        }
        if (actor == 3 && !file->FAdoptChild(kctgActr, actorNumber, kctgTmpl, header.tagTmpl.cno, 0))
        {
            ok = false;
            break;
        }
        actors[actor] = actorNumber;
        if (movie)
        {
            RollCallActorEntryOnFile entry = {};
            entry.arid = actor;
            entry.cactRef = 2;
            entry.tagTmpl = header.tagTmpl;
            String name;
            name.SetSzs(actor == 0 ? PszLit("Animated actor") : actor == 3 ? PszLit("3D") : PszLit("Prop"));
            ok = roll && roll->FAddStn(&name, &entry);
        }
        ActorTrack track;
        if (!track.FCreate())
        {
            ok = false;
            break;
        }
        const int distance = movie ? 400 : 40;
        RouteDistancePoint points[] = {{{rZero, rZero, rZero}, BR_SCALAR(distance)},
                                       {{BR_SCALAR(distance), rZero, rZero}, rZero}};
        if (!track.route->FInsert(0, &points[0]) || !track.route->FInsert(1, &points[1]))
        {
            ok = false;
            break;
        }
        ActorEvent::Base event = {ActorEvent::aetAdd, 1, {0, rZero, 0}};
        ActorEvent::Add addition = {};
        ActorEvent::Action action = {movie && actor == 0 ? animation : 0, 0};
        BRS step = BR_SCALAR(2);
        ok = track.events->FInsert(0, sizeof(addition), &addition, &event);
        event.aet = ActorEvent::aetActn;
        ok = ok && track.events->FInsert(1, sizeof(action), &action, &event);
        event.aet = ActorEvent::aetStep;
        ok = ok && track.events->FInsert(2, sizeof(step), &step, &event);
        if (movie && actor == 0)
            ok = ok && Costume(track, 8, costumeSet, alternateCostume, pvNil, 0) &&
                 Costume(track, 20, costumeSet, originalCostume, pvNil, 0);
        if (movie && actor == 2)
            ok =
                ok && Costume(track, 1, 0, 0, document, textures[0]) && Costume(track, 12, 0, 0, document, textures[1]);
        if (movie && actor == 2)
            for (unsigned texture = 0; texture < 2; ++texture)
                ok = ok && file->FAdoptChild(kctgActr, actorNumber, kctgMtrl, textures[texture], texture);
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
    TAGOnFile background = {source, 0, kctgBkgd, 0};
    SceneEvent event = {1, sevtSetBkgd};
    ok = ok && starts->FInsert(starts->IvMac(), sizeof(background), &background, &event);
    std::uint32_t nextCamera = 1;
    SceneEvent change = {movie ? 15 : 5, sevtChngCamera};
    ok = ok && frames->FInsert(0, sizeof(nextCamera), &nextCamera, &change);
    ChunkNumber credits = 0;
    if (batch)
    {
        std::uint32_t slot = 0;
        SceneEvent start = {1, sevtAddTbox};
        ok = ok && BatchPauses(frames, 0) && BatchCredits(file, &credits) &&
             file->FAdoptChild(kctgScen, sceneNumber, kctgTbox, credits, slot) &&
             starts->FInsert(starts->IvMac(), sizeof(slot), &slot, &start);
    }
    ok = ok && SceneData::FWriteEvents(file, sceneNumber, frames, starts) && file->FSave(kctgMvie);
    // Preview mode compares against a background-only scene. Movie mode keeps
    // the actors and authors a different camera sequence in the second scene.
    ChunkNumber emptyScene;
    if (dissolve)
        scene.transition = transDissolve;
    ok = ok && scene.FCreate(file, &emptyScene);
    if (!movie)
        for (int i = 0; i < 5; ++i)
            starts->Delete(0);
    else
        for (int i = 0; ok && i < count; ++i)
            ok = file->FAdoptChild(kctgScen, emptyScene, kctgActr, actors[i], i);
    if (batch)
        ok = ok && file->FAdoptChild(kctgScen, emptyScene, kctgTbox, credits, 0);
    auto secondFrames = movie ? GeneralGroup::PggNew(sizeof(SceneEvent)) : pvNil;
    if (movie)
    {
        std::uint32_t firstCamera = 1, laterCamera = 0;
        SceneEvent first = {1, sevtChngCamera}, later = {15, sevtChngCamera};
        ok = ok && secondFrames && secondFrames->FInsert(0, sizeof(firstCamera), &firstCamera, &first) &&
             secondFrames->FInsert(1, sizeof(laterCamera), &laterCamera, &later);
        if (batch)
            ok = ok && BatchPauses(secondFrames, 1);
    }
    ok =
        ok && SceneData::FWriteEvents(file, emptyScene, movie ? secondFrames : frames, starts) && file->FSave(kctgMvie);
    ReleasePpo(&secondFrames);
    if (movie)
    {
        ChunkNumber root, child;
        DataBlock block;
        auto sources = StringTable_GST::PgstNew(sizeof(LegacyLong));
        String name;
        name.SetSzs(PszLit("3D Movie Maker/3DMOVIE"));
        LegacyLong sid = 2;
        ok = ok && sources && sources->FAddStn(&name, &sid) && MovieData::FCreateRoot(file, &root) &&
             file->FAdoptChild(kctgMvie, root, kctgScen, sceneNumber, 0) &&
             file->FAdoptChild(kctgMvie, root, kctgScen, emptyScene, 1) &&
             file->FAddChild(kctgMvie, root, 0, roll->CbOnFile(), kctgGst, &child, &block) && roll->FWrite(&block) &&
             file->FAddChild(kctgMvie, root, 1, sources->CbOnFile(), kctgGst, &child, &block) &&
             sources->FWrite(&block) && file->FSave(kctgSoc);
        ReleasePpo(&sources);
    }
    ReleasePpo(&starts);
    ReleasePpo(&frames);
    ReleasePpo(&file);
    ReleasePpo(&roll);
    ReleasePpo(&document);
    ReleasePpo(&animated);
    ReleasePpo(&templateResources);
    ReleasePpo(&templateFile);
    ReleasePpo(&shadeFile);
    Body::ReleaseSharedMaterial();
    Material_MTRL::ReleaseShadeTable();
    BrZbEnd();
    BrEnd();
    return ok ? 0 : 1;
}
