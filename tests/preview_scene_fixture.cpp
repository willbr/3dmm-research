/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>
#include "soc_core.h"
#include "scene_archive_fixture.h"
#include "actor_track_fixture.h"
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
    auto map = EngineTexture::New(pixels, 16, 16);
    ChunkNumber texture;
    MaterialOnFile header = {kboCur, koskCur, 0xffffff, BR_UFRACTION(.4), BR_UFRACTION(.6), 0, 15, 15, BR_SCALAR(50)};
    const bool ok = map && EngineTexture::Write(map, file, kctgTmap, &texture) &&
                    file->FAddPv(&header, sizeof(header), kctgMtrl, material) &&
                    file->FAdoptChild(kctgMtrl, *material, kctgTmap, texture, 0);
    if (map)
        mm_texture_owner_release(map);
    else
        FreePpv(reinterpret_cast<void **>(&pixels));
    return ok;
}

static bool Costume(ActorTrack &track, int frame, int set, int cmid, PChunkyResourceFile resource, ChunkNumber material)
{
    ActorEvent::Base event = {ActorEvent::aetCost, frame, {0, BR_SCALAR((frame - 1) * 2), 0}};
    ActorEvent::Costume costume = {};
    costume.ibset = set;
    costume.cmid = cmid;
    costume.fCmtl = resource == pvNil ? tYes : tNo;
    costume.tag = {resource ? 0 : 2, resource ? resource->CoreCache() : nullptr, kctgMtrl, material};
    TagManager::DupTag(&costume.tag);
    if (mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()), sizeof(costume), &costume, &event))
        return true;
    TagManager::CloseTag(&costume.tag);
    return false;
}
static bool AnimationEvents(ActorTrack &track, PChunkyResourceFile resource, ChunkNumber material)
{
    const auto append = [&](int kind, int offset, int hold, const void *value, int bytes, int cached = 900) {
        ActorEvent::Base event = {kind, cached, {0, BR_SCALAR(offset), hold}};
        return mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()), bytes, const_cast<void *>(value), &event);
    };
    int32_t freeze = 1, unfreeze = 0;
    BRS size = BR_SCALAR(1.5), stop = rZero, speed = BR_SCALAR(2);
    ActorEvent::Action action = {0, 0};
    BMAT34 rotation;
    BrMatrix34RotateY(&rotation, BR_ANGLE_DEG(35));
    ActorEvent::Stretch stretch = {BR_SCALAR(1.2), BR_SCALAR(.8), rOne};
    RoutePoint move = {rOne, BR_SCALAR(2), rZero}, tweak = {BR_SCALAR(31), rZero, rZero};
    ActorEvent::Add entrance = {};
    return append(ActorEvent::aetFreeze, 6, 0, &freeze, sizeof(freeze)) &&
           append(ActorEvent::aetFreeze, 10, 0, &unfreeze, sizeof(unfreeze)) &&
           append(ActorEvent::aetActn, 12, 0, &action, sizeof(action)) &&
           append(ActorEvent::aetSize, 16, 0, &size, sizeof(size)) &&
           append(ActorEvent::aetStep, 22, 0, &stop, sizeof(stop)) &&
           append(ActorEvent::aetRotF, 22, 1, &rotation, sizeof(rotation)) &&
           append(ActorEvent::aetStep, 22, 2, &speed, sizeof(speed)) &&
           append(ActorEvent::aetPull, 25, 0, &stretch, sizeof(stretch)) &&
           append(ActorEvent::aetMove, 28, 0, &move, sizeof(move)) &&
           append(ActorEvent::aetTweak, 31, 0, &tweak, sizeof(tweak)) &&
           append(ActorEvent::aetRem, 34, 0, nullptr, 0) &&
           append(ActorEvent::aetAdd, 36, 0, &entrance, sizeof(entrance), 22) &&
           append(ActorEvent::aetActn, 36, 0, &action, sizeof(action)) && Costume(track, 20, 0, 0, resource, material);
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
static bool BatchCredits(PChunkyFile file, ChunkNumber *number, bool story = false, bool regression = false)
{
    RichTextData text;
    text.fontName.SetSzs(PszLit("Arial"));
    text.defaults.dypFont = 16;
    text.defaults.dxpDef = 180;
    text.defaults.lwAcrBack = 0x00ffffff;
    text.properties = mm_text_collection_new(MM_TEXT_PROPERTIES, nullptr);
    text.text.pfil = FileObject::PfilCreateTemp();
    char credits[4096];
    int count = 0;
    for (int line = 0; line < (story ? 3 : regression ? 8 : 24); ++line)
        count += std::snprintf(credits + count, sizeof(credits) - count,
                              story ? "Static words %d: Aa Zz 0123! Wrapping sentence across the box.\r"
                                    : "Scrolling credits line %d\r", line + 1);
    text.text.cb = count;
    if (!text.properties || !text.text.pfil || !text.text.pfil->FSetFpMac(count) ||
        !text.text.FWriteRgb(credits, count, 0))
        return false;
    ChunkIdentification rich;
    TextBoxData box;
    box.state.first = regression ? 3 : -2;
    box.state.end = regression && story ? 25 : 31;
    box.state.story = story;
    box.SetBounds(40, 24, 220, story ? 160 : 72);
    return text.FWrite(file, &rich) && file->FAdd(0, kctgTbox, number) && box.FWrite(file, *number) &&
           file->FAdoptChild(kctgTbox, *number, rich.ctg, rich.cno, 0);
}

// Audible regression cues have distinct frequencies and silence between them.
// Store explicit little-endian PCM bytes rather than host-dependent structs.
static bool AudioCues(PChunkyFile file, ChunkNumber scene, PGeneralGroup frames,
                      ChunkNumber *numbers = nullptr, bool sceneCues = true)
{
    for (int cue = 0; cue < 2; ++cue)
    {
        struct Header
        {
            int16_t bo, osk;
            int32_t style, volume, invalid;
        } header = {kboCur, koskCur, cue ? 2 : 3, 0x10000, 0};
        std::vector<byte> bytes;
        const auto word = [&](std::uint32_t value, int count) {
            for (int i = 0; i < count; ++i)
                bytes.push_back(static_cast<byte>(value >> (i * 8)));
        };
        const unsigned samples = 5512, rate = 11025;
        bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
        word(36 + samples * 2, 4);
        bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        word(16, 4);
        word(1, 2);
        word(1, 2);
        word(rate, 4);
        word(rate * 2, 4);
        word(2, 2);
        word(16, 2);
        bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
        word(samples * 2, 4);
        for (unsigned i = 0; i < samples; ++i)
            word(static_cast<std::uint16_t>(
                     static_cast<std::int16_t>(2048 * std::sin(6.283185307179586 * (cue ? 1300 : 600) * i / rate))),
                 2);
        ChunkNumber sound, wave;
        if (!file->FAddPv(&header, sizeof(header), kctgMsnd, &sound) ||
            !file->FAddPv(bytes.data(), static_cast<LegacyLong>((bytes.size)()), kctgWave, &wave) ||
            !file->FAdoptChild(kctgMsnd, sound, kctgWave, wave, 0) ||
            !file->FAdoptChild(kctgScen, scene, kctgMsnd, sound, cue))
            return false;
        String name;
        name.SetSzs(cue ? PszLit("Regression effect 1300 Hz") : PszLit("Regression speech 600 Hz"));
        if (!file->FSetName(kctgMsnd, sound, &name))
            return false;
        if (numbers)
            numbers[cue] = sound;
        if (!sceneCues)
            continue;
        SceneEvent event = {cue ? 25 : 1, sevtPlaySnd};
        std::int32_t data[] = {0x10000, cue ? 2 : 3, 0, 1, cue, 0, 0, kctgMsnd, static_cast<std::int32_t>(sound)};
        LegacyLong insertion = 0;
        for (; insertion < frames->IvMac(); ++insertion)
        {
            SceneEvent existing;
            frames->GetFixed(insertion, &existing);
            if (existing.nfrm > event.nfrm)
                break;
        }
        if (!frames->FInsert(insertion, sizeof(data), data, &event))
            return false;
    }
    return true;
}

static bool ActorAudioEvents(ActorTrack &track, PChunkyResourceFile resource, const ChunkNumber *sounds)
{
    // Bind two cels, queue two ordinary cues, then replace the first binding.
    // Both scene instances reuse the track; scene-child lookup resolves its tags.
    const int frames[] = {1, 1, 3, 3, 10};
    const int cels[] = {1, 3, -1, -1, 1};
    const int volumes[] = {0x10000, 0x6000, 0x10000, 0x8000, 0x8000};
    for (int i = 0; i < 5; ++i)
    {
        ActorEvent::Base event = {ActorEvent::aetSnd, frames[i], {0, BR_SCALAR((frames[i] - 1) * 2), 0}};
        MMActorSound sound = {};
        sound.cel = cels[i];
        sound.style = cels[i] < 0 ? 3 : 2;
        sound.volume = volumes[i];
        sound.queue = cels[i] < 0 ? tYes : tNo;
        sound.child = cels[i] < 0 ? 0 : 1;
        sound.tag = {0, resource->CoreCache(), kctgMsnd, sounds[sound.child]};
        mm_tag_dup(&sound.tag, mm_tag_local_cache_services());
        LegacyLong index = 0;
        for (; index < mm_actor_events_count(track.Events()); ++index)
        {
            ActorEvent::Base old;
            mm_actor_events_get_fixed(track.Events(), index, &old);
            if (old.rtel.dwrOffset > event.rtel.dwrOffset)
                break;
        }
        if (!mm_actor_events_insert(track.Events(), index, sizeof(sound), &sound, &event))
        {
            mm_tag_close(&sound.tag, mm_tag_local_cache_services());
            return false;
        }
    }
    return true;
}


#include "textbox_acid_fixture.h"

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5 &&
        !(argc == 6 && (std::strcmp(argv[5], "--batch") == 0 || std::strcmp(argv[5], "--dissolve") == 0 ||
                        std::strcmp(argv[5], "--events") == 0 || std::strcmp(argv[5], "--audio") == 0 ||
                        std::strcmp(argv[5], "--actor-audio") == 0 ||
                        std::strcmp(argv[5], "--static-text") == 0 || std::strcmp(argv[5], "--scrolling-text") == 0 || std::strcmp(argv[5], "--text-acid") == 0)))
    {
        std::fprintf(stderr, "Usage: preview-scene-fixture OUT BACKGROUNDS MAIN_CHK [TEMPLATES "
                             "[--batch|--dissolve|--events|--audio|--static-text|--scrolling-text|--text-acid]]\n");
        return 2;
    }
    const bool movie = argc >= 5;
    const bool batch = argc == 6 && std::strcmp(argv[5], "--batch") == 0;
    const bool dissolve = argc == 6 && std::strcmp(argv[5], "--dissolve") == 0;
    const bool events = argc == 6 && std::strcmp(argv[5], "--events") == 0;
    const bool audio = argc == 6 && std::strcmp(argv[5], "--audio") == 0;
    const bool actorAudio = argc == 6 && std::strcmp(argv[5], "--actor-audio") == 0;
    const bool staticText = argc == 6 && std::strcmp(argv[5], "--static-text") == 0;
    const bool scrollingText = argc == 6 && std::strcmp(argv[5], "--scrolling-text") == 0;
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
    if (argc == 6 && std::strcmp(argv[5], "--text-acid") == 0)
    {
        const bool ok = TextAcid::Write(file);
        ReleasePpo(&file);
        return ok ? 0 : 1;
    }
    auto scene = SceneArchiveFixture::State();
    ChunkNumber sceneNumber;
    scene.firstFrame = batch ? -2 : 1;
    scene.lastFrame = movie ? 30 : 10;
    // Transitions belong to the outgoing scene. Put the dissolve on the
    // first scene so ordinary playback actually exercises the compositor.
    if (dissolve)
        scene.transition = transDissolve;
    bool ok = SceneArchiveFixture::Create(file, &sceneNumber, scene);
    auto frames = GeneralGroup::PggNew(sizeof(SceneEvent));
    auto starts = GeneralGroup::PggNew(sizeof(SceneEvent));
    if (!frames || !starts)
        ok = false;
    BrBegin();
    BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
    PChunkyFile shadeFile = Open(argv[3]);
    ok = ok && shadeFile && EngineMaterial::SetShadeTable(shadeFile, kctgTmap, 0);
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
            animated = EngineTemplate::Fetch(templateResources, kctgTmpl, 0x2010);
        ok = ok && animated;
        bool found = false;
        for (LegacyLong action = 0; ok && action < EngineTemplate::Cactn(animated); ++action)
        {
            LegacyLong cells = 0;
            if (EngineTemplate::FGetCcelActn(animated, action, &cells) && cells >= 6)
            {
                animation = action;
                found = true;
                break;
            }
        }
        auto body = animated ? EngineTemplate::PbodyCreate(animated) : pvNil;
        for (LegacyLong set = 0; body && set < EngineBody::SetCount(body); ++set)
            if (!EngineTemplate::FBsetIsAccessory(animated, set) && EngineTemplate::CcmidOfBset(animated, set) > 1)
            {
                costumeSet = set;
                originalCostume = EngineTemplate::CmidOfBset(animated, set, 0);
                alternateCostume = EngineTemplate::CmidOfBset(animated, set, 1);
                break;
            }
        mm_body_owner_release(body);
        ok = ok && found && costumeSet >= 0 && Texture(file, 0, &textures[0]) && Texture(file, 1, &textures[1]);
        std::printf("Feature fixture: action=%d costume_set=%d costumes=%d/%d\n", int(animation), int(costumeSet),
                    int(originalCostume), int(alternateCostume));
    }
    ChunkNumber actors[5] = {};
    ChunkNumber actorSounds[2] = {};
    if (actorAudio)
        ok = ok && AudioCues(file, sceneNumber, frames, actorSounds, false);
    auto roll = movie ? StringTable_GST::PgstNew(sizeof(RollCallActorEntryOnFile)) : pvNil;
    const int count = movie ? 4 : 5;
    for (int actor = 0; ok && actor < count; ++actor)
    {
        ChunkNumber actorNumber;
        DataBlock block;
        MMActorHeader header = {};
        header.order = kboCur;
        header.system = koskCur;
        header.actor_id = actor;
        header.first_frame = 1;
        header.last_frame = scene.lastFrame;
        if (actor == 4)
            header.first_frame = header.last_frame = knfrmInvalid;
        header.source_id = source;
        header.template_type = kctgTmpl;
        header.template_number = actor == 0 ? 0x2010 : actor == 1 ? 0x1020 : 0x1010;
        header.translation[0] = static_cast<MMU32>(BrsAdd(camera.apos.xrPlace, BR_SCALAR(actor * 12 - 18)));
        header.translation[1] = static_cast<MMU32>(camera.apos.yrPlace);
        header.translation[2] = static_cast<MMU32>(camera.apos.zrPlace);
        if (actor == 3)
        {
            String text;
            text.SetSzs(PszLit("3D"));
            TAG font = {source, pvNil, kctgTdf, 0};
            auto templateText = EngineText::PtdtNew(&text, tdtsNormal, &font);
            ChunkNumber templateNumber;
            const bool written = templateText && EngineText::FWrite(templateText, file, kctgTmpl, &templateNumber);
            mm_template_owner_release(templateText);
            if (!written)
            {
                ok = false;
                break;
            }
            header.source_id = 0;
            header.template_number = templateNumber;
        }
        byte oldHeader[MM_ACTOR_OLD_HEADER_BYTES];
        const size_t prefix = offsetof(MMActorHeader, last_frame);
        CopyPb(&header, oldHeader, prefix);
        CopyPb(&header.source_id, oldHeader + prefix, sizeof(oldHeader) - prefix);
        const LegacyLong headerBytes = actor == 1 && !movie ? sizeof(oldHeader) : sizeof(header);
        if (!file->FAddChild(kctgScen, sceneNumber, actor, headerBytes, kctgActr, &actorNumber, &block) ||
            !block.FWriteRgb(actor == 1 && !movie ? static_cast<void *>(oldHeader) : static_cast<void *>(&header),
                             headerBytes, 0))
        {
            ok = false;
            break;
        }
        if (actor == 3 && !file->FAdoptChild(kctgActr, actorNumber, kctgTmpl, header.template_number, 0))
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
            entry.tagTmpl = {header.source_id, header.wire_pointer_pad, header.template_type, header.template_number};
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
        if (!mm_actor_path_insert(track.Route(), 0, &points[0]) || !mm_actor_path_insert(track.Route(), 1, &points[1]))
        {
            ok = false;
            break;
        }
        ActorEvent::Base event = {ActorEvent::aetAdd, 1, {0, rZero, 0}};
        ActorEvent::Add addition = {};
        ActorEvent::Action action = {movie && actor == 0 ? animation : 0, 0};
        BRS step = BR_SCALAR(2);
        ok = mm_actor_events_insert(track.Events(), 0, sizeof(addition), &addition, &event);
        event.aet = ActorEvent::aetActn;
        ok = ok && mm_actor_events_insert(track.Events(), 1, sizeof(action), &action, &event);
        event.aet = ActorEvent::aetStep;
        ok = ok && mm_actor_events_insert(track.Events(), 2, sizeof(step), &step, &event);
        if (movie && actor == 0)
            ok = ok && Costume(track, 8, costumeSet, alternateCostume, pvNil, 0) &&
                 Costume(track, 20, costumeSet, originalCostume, pvNil, 0);
        if (movie && actor == 2)
            ok =
                ok && Costume(track, 1, 0, 0, document, textures[0]) && Costume(track, 12, 0, 0, document, textures[1]);
        if (movie && actor == 2)
            for (unsigned texture = 0; texture < 2; ++texture)
                ok = ok && file->FAdoptChild(kctgActr, actorNumber, kctgMtrl, textures[texture], texture);
        if (events && actor == 1)
            ok = ok && AnimationEvents(track, document, textures[0]);
        if (actorAudio && actor == 0)
            ok = ok && ActorAudioEvents(track, document, actorSounds);
        if (actor == 4)
        {
            mm_array_set_count(&track.Route()->rows, 0);
            while (mm_actor_events_count(track.Events()))
                mm_actor_events_delete(track.Events(), 0);
        }
        ChunkNumber child;
        ok = ok && file->FAddChild(kctgActr, actorNumber, 0, mm_actor_path_wire_size(track.Route()), kctgPath, &child, &block) &&
             mm_actor_write_path_block(track.Route(), koskCur, block.CoreBlock());
        auto wire = mm_actor_encode_events(track.Events());
        ok = ok && wire && file->FAddChild(kctgActr, actorNumber, 0, mm_actor_events_wire_size(wire), kctgGgae, &child, &block) &&
             mm_actor_write_events_block(wire, koskCur, 0, block.CoreBlock());
        mm_actor_events_release(wire);
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
    if (batch || staticText || scrollingText)
    {
        std::uint32_t slot = 0;
        SceneEvent start = {1, sevtAddTbox};
        ok = ok && (!batch || BatchPauses(frames, 0)) &&
             BatchCredits(file, &credits, staticText, staticText || scrollingText) &&
             file->FAdoptChild(kctgScen, sceneNumber, kctgTbox, credits, slot) &&
             starts->FInsert(starts->IvMac(), sizeof(slot), &slot, &start);
    }
    if (audio)
        ok = ok && AudioCues(file, sceneNumber, frames);
    ok = ok && SceneArchiveFixture::WriteEvents(file, sceneNumber, frames, starts) && file->FSave(kctgMvie);
    // Preview mode compares against a background-only scene. Movie mode keeps
    // the actors and authors a different camera sequence in the second scene.
    ChunkNumber emptyScene;
    scene.transition = transCut;
    ok = ok && SceneArchiveFixture::Create(file, &emptyScene, scene);
    if (!movie)
        for (int i = 0; i < 5; ++i)
            starts->Delete(0);
    else
        for (int i = 0; ok && i < count; ++i)
            ok = file->FAdoptChild(kctgScen, emptyScene, kctgActr, actors[i], i);
    if (batch || staticText || scrollingText)
        ok = ok && file->FAdoptChild(kctgScen, emptyScene, kctgTbox, credits, 0);
    if (actorAudio)
        for (unsigned cue = 0; cue < 2; ++cue)
            ok = ok && file->FAdoptChild(kctgScen, emptyScene, kctgMsnd, actorSounds[1 - cue], cue);
    auto secondFrames = movie ? GeneralGroup::PggNew(sizeof(SceneEvent)) : pvNil;
    if (movie)
    {
        std::uint32_t firstCamera = 1, laterCamera = 0;
        SceneEvent first = {1, sevtChngCamera}, later = {15, sevtChngCamera};
        ok = ok && secondFrames && secondFrames->FInsert(0, sizeof(firstCamera), &firstCamera, &first) &&
             secondFrames->FInsert(1, sizeof(laterCamera), &laterCamera, &later);
        if (batch)
            ok = ok && BatchPauses(secondFrames, 1);
        if (audio)
            ok = ok && AudioCues(file, emptyScene, secondFrames);
    }
    ok =
        ok && SceneArchiveFixture::WriteEvents(file, emptyScene, movie ? secondFrames : frames, starts) && file->FSave(kctgMvie);
    ReleasePpo(&secondFrames);
    if (movie)
    {
        ChunkNumber root, child;
        DataBlock block;
        // Saved first-scene thumbnail; the second scene deliberately has none.
        byte pixels[8 * 6];
        std::memset(pixels, 32, sizeof(pixels));
        RC bounds(0, 0, 8, 6);
        auto thumbnail = NewMaskedBitmap(pixels, 8, 6, &bounds, 0, 0, 0);
        ok = ok && thumbnail &&
             file->FAddChild(kctgScen, sceneNumber, 0, MaskedBitmapSize(thumbnail), kctgThumbMbmp, &child, &block) &&
             WriteMaskedBitmap(thumbnail, &block);
        ReleaseMaskedBitmap(&thumbnail);
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
    mm_template_owner_release(animated);
    ReleasePpo(&templateResources);
    ReleasePpo(&templateFile);
    ReleasePpo(&shadeFile);
    EngineBody::ReleaseSharedMaterial();
    EngineMaterial::ReleaseShadeTable();
    BrZbEnd();
    BrEnd();
    return ok ? 0 : 1;
}
