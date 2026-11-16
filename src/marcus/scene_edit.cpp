#include "scene_edit.h"
#include "mm/content_vfs.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include "soc_core.h"
#include "mm/scene_runtime.h"
#include "storyboard_native.h"
ASSERTNAME

namespace
{
struct Release { template<class T> void operator()(T *value) const { if (value) value->Release(); } };
void Require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
}
static std::shared_ptr<MarcusMovieFile> WorkingCopy(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin)
{
    namespace fs = std::filesystem;
    auto result = std::make_shared<MarcusMovieFile>(fs::path{});
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 100; ++attempt)
    {
        auto path = fs::temp_directory_path() / ("marcus-edit-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        if (fs::create_directory(path)) { result->directory = path; break; }
    }
    Require(!result->directory.empty(), "Cannot reserve a new movie working file.");
    result->path = result->directory / "movie.3mm";
    if (origin)
    {
        // Retain the original dependencies, rather than every superseded copy.
        auto dependencies = origin;
        while (dependencies->origin) dependencies = dependencies->origin;
        result->origin = dependencies;
        result->contentOrigin = origin->contentOrigin.empty() ? dependencies->path : origin->contentOrigin;
        result->fonts = origin->fonts;
        result->fontFiles = origin->fontFiles;
        result->fontDirectory = origin->fontDirectory;
    }
    if (!source.empty())
    {
        fs::copy_file(source, result->path);
        fs::permissions(result->path, fs::perms::owner_write, fs::perm_options::add);
    }
    return result;
}
static LegacyLong RetailSource(PChunkyFile file, ChunkNumber movie, LegacyLong fallbackSource)
{
    ChildChunkIdentification child;
    DataBlock block;
    short order = kboCur;
    const bool hadSources = file->FGetKidChidCtg(kctgMvie, movie, 1, kctgGst, &child);
    std::unique_ptr<StringTable_GST, Release> sources;
    if (hadSources)
    {
        Require(file->FFind(kctgGst, child.cki.cno, &block), "Cannot read movie content sources.");
        sources.reset(StringTable_GST::PgstRead(&block, &order));
    }
    else sources.reset(StringTable_GST::PgstNew(sizeof(LegacyLong)));
    Require(sources && sources->CbExtra() == sizeof(LegacyLong), "The movie content source table is invalid.");
    LegacyLong sourceId = -1, maxSource = 2;
    for (LegacyLong index = 0; index < sources->IvMac(); ++index)
    {
        LegacyLong id;
        sources->GetExtra(index, &id);
        if (order == kboOther) { SwapBytesRglw(&id, 1); sources->PutExtra(index, &id); }
        maxSource = std::max(maxSource, id);
        String title;
        schar text[1024];
        sources->GetStn(index, &title);
        title.GetSzs(text);
        if (id > 0 && MM::Content::SourceKey(text) == "3dmovie") sourceId = id;
    }
    String sourceName;
    sourceName.SetSzs(PszLit("3D Movie Maker/3DMOVIE"));
    if (!hadSources)
    {
        LegacyLong fallback = fallbackSource;
        Require(sources->FAddStn(&sourceName, &fallback), "Cannot retain original content source.");
        sourceId = fallback;
    }
    if (sourceId < 0)
    {
        Require(maxSource < std::numeric_limits<LegacyLong>::max(), "This movie has no available content source IDs.");
        sourceId = maxSource + 1;
        Require(sources->FAddStn(&sourceName, &sourceId), "Cannot register the scene background's content source.");
    }
    ChunkNumber sourceChunk;
    Require(file->FAdd(sources->CbOnFile(), kctgGst, &sourceChunk, &block) &&
            sources->FWrite(&block), "Cannot write movie content sources.");
    if (hadSources) file->DeleteChild(kctgMvie, movie, kctgGst, child.cki.cno, 1);
    Require(file->FAdoptChild(kctgMvie, movie, kctgGst, sourceChunk, 1), "Cannot link movie content sources.");
    return sourceId;
}
static void ApplyNewScene(PChunkyFile file, const MM::PreviewSceneDefinition &definition, int ordinal, bool emptyMovie)
{
    ChunkIdentification root;
    const bool hadRoot = file->FGetCkiCtg(kctgMvie, 0, &root);
    if (!hadRoot)
    {
        root.ctg = kctgMvie;
        Require(MovieData::FCreateRoot(file, &root.cno), "Cannot create the movie header.");
        // Headless scene-only inputs retain their original ordered scene list.
        ChunkIdentification existing;
        for (LegacyLong index = 0; file->FGetCkiCtg(kctgScen, index, &existing); ++index)
            Require(file->FAdoptChild(kctgMvie, root.cno, kctgScen, existing.cno, index), "Cannot retain an existing scene.");
    }
    ChildChunkID slot = 0;
    ChildChunkIdentification child;
    for (LegacyLong index = 0; file->FGetKid(kctgMvie, root.cno, index, &child); ++index)
        if (child.cki.ctg == kctgScen)
        {
            Require(child.chid < 0x7fffffffUL, "This movie has no available scene slots.");
            slot = std::max(slot, child.chid + 1);
        }
    const auto sourceId = RetailSource(file, root.cno, hadRoot || emptyMovie ? 2 : 1);
    SceneData scene;
    scene.firstFrame = scene.lastFrame = scene.currentFrame = 1;
    scene.transition = transCut;
    String name;
    const auto label = "Scene " + std::to_string(ordinal);
    name.SetSzs(const_cast<char *>(label.c_str()));
    scene.SetName(&name);
    ChunkNumber number;
    std::unique_ptr<GeneralGroup, Release> starts(GeneralGroup::PggNew(sizeof(SceneEvent)));
    std::unique_ptr<GeneralGroup, Release> frames(GeneralGroup::PggNew(sizeof(SceneEvent)));
    TAGOnFile tag = {sourceId, 0, kctgBkgd, definition.background};
    SceneEvent event = {1, sevtSetBkgd};
    Require(!file->FFind(kctgScen, definition.number) && starts && frames &&
            starts->FInsert(0, sizeof(tag), &tag, &event) && scene.FCreate(file, &number), "Cannot create the new scene.");
    if (number != definition.number) { file->Move(kctgScen, number, kctgScen, definition.number); number = definition.number; }
    Require(SceneData::FWriteEvents(file, number, frames.get(), starts.get()) &&
            file->FAdoptChild(kctgMvie, root.cno, kctgScen, number, slot), "Cannot add the new scene to the movie.");
}
static void ApplyCamera(PChunkyFile file, unsigned long scene, long frame, unsigned long camera)
{
    ChildChunkIdentification child;
    DataBlock block;
    Require(file->FGetKidChidCtg(kctgScen, scene, 0, kctgFrmGg, &child) &&
            file->FFind(kctgFrmGg, child.cki.cno, &block), "Cannot read scene camera events.");
    short order;
    std::unique_ptr<GeneralGroup, Release> frames(SceneData::PggReadEvents(&block, &order, false));
    Require(bool(frames), "The scene frame events are invalid.");
    // Preserve all other events and their payload byte order, including opaque
    // sound records. Replace every camera change at this frame with one choice.
    for (LegacyLong index = frames->IvMac(); index-- > 0;)
    {
        SceneEvent event;
        frames->GetFixed(index, &event);
        if (order == kboOther) SwapBytesBom(&event, kbomSev);
        if (event.nfrm == frame && event.sevt == sevtChngCamera) frames->Delete(index);
    }
    LegacyLong insertion = frames->IvMac();
    for (LegacyLong index = 0; index < frames->IvMac(); ++index)
    {
        SceneEvent event;
        frames->GetFixed(index, &event);
        if (order == kboOther) SwapBytesBom(&event, kbomSev);
        if (event.nfrm > frame) { insertion = index; break; }
    }
    SceneEvent event = {static_cast<int32_t>(frame), sevtChngCamera};
    std::uint32_t value = static_cast<std::uint32_t>(camera);
    if (order == kboOther) { SwapBytesBom(&event, kbomSev); SwapBytesRglw(&value, 1); }
    ChunkNumber number;
    Require(frames->FInsert(insertion, sizeof(value), &value, &event) &&
            file->FAdd(frames->CbOnFile(), kctgFrmGg, &number, &block) && frames->FWrite(&block, order),
            "Cannot write the scene camera change.");
    file->DeleteChild(kctgScen, scene, kctgFrmGg, child.cki.cno, 0);
    Require(file->FAdoptChild(kctgScen, scene, kctgFrmGg, number, 0), "Cannot link the scene camera change.");
    SceneData metadata;
    Require(metadata.FRead(file, scene), "Cannot read scene metadata.");
    if (frame == metadata.firstFrame && file->FGetKidChidCtg(kctgScen, scene, 0, kctgThumbMbmp, &child))
        file->DeleteChild(kctgScen, scene, kctgThumbMbmp, child.cki.cno, 0);
}
static void ApplySceneRange(PChunkyFile file, const MarcusSceneRange &range)
{
    const auto sceneNumber = range.scene;
    SceneData scene;
    Require(scene.FRead(file, sceneNumber), "Cannot read the scene frame range.");
    Require(range.first > std::numeric_limits<std::int32_t>::min() && range.first <= scene.firstFrame &&
            range.last >= scene.lastFrame && range.last <= std::numeric_limits<std::int32_t>::max(),
            "The scene range must retain its original frames.");
    if (range.first < scene.firstFrame)
    {
        ChildChunkIdentification child;
        DataBlock block;
        short order = kboCur;
        Require(file->FGetKidChidCtg(kctgScen, sceneNumber, 0, kctgFrmGg, &child) &&
                file->FFind(kctgFrmGg, child.cki.cno, &block), "Cannot read scene frame events.");
        std::unique_ptr<GeneralGroup, Release> frames(SceneData::PggReadEvents(&block, &order, false));
        Require(bool(frames), "Cannot decode scene frame events.");
        struct Host { PGeneralGroup frames; short order; } host{frames.get(), order};
        MMSceneRuntimeServices services = {};
        services.context = &host;
        services.event_count = [](void *p) -> MMI32 { return static_cast<Host *>(p)->frames->IvMac(); };
        services.event = [](void *p, MMI32 index, MMI32 *at, int *kind) {
            auto &host = *static_cast<Host *>(p);
            SceneEvent event;
            host.frames->GetFixed(index, &event);
            if (host.order == kboOther) SwapBytesBom(&event, kbomSev);
            *at = event.nfrm;
            *kind = event.sevt;
        };
        services.move_camera = [](void *p, MMI32 index, MMI32 at) {
            auto &host = *static_cast<Host *>(p);
            SceneEvent event;
            host.frames->GetFixed(index, &event);
            if (host.order == kboOther) SwapBytesBom(&event, kbomSev);
            event.nfrm = at;
            if (host.order == kboOther) SwapBytesBom(&event, kbomSev);
            host.frames->PutFixed(index, &event);
            host.frames->Move(index, 0);
        };
        // Share retail's opening-camera policy, including event ordering.
        mm_scene_move_first(&scene, static_cast<MMI32>(range.first), &services);
        ChunkNumber number;
        Require(file->FAdd(frames->CbOnFile(), kctgFrmGg, &number, &block) && frames->FWrite(&block, order),
                "Cannot write the extended scene frame events.");
        file->DeleteChild(kctgScen, sceneNumber, kctgFrmGg, child.cki.cno, 0);
        Require(file->FAdoptChild(kctgScen, sceneNumber, kctgFrmGg, number, 0), "Cannot link extended scene frame events.");
        if (file->FGetKidChidCtg(kctgScen, sceneNumber, 0, kctgThumbMbmp, &child))
            file->DeleteChild(kctgScen, sceneNumber, kctgThumbMbmp, child.cki.cno, 0);
    }
    scene.lastFrame = static_cast<MMI32>(range.last);
    Require(scene.FWrite(file, sceneNumber), "Cannot write the extended scene.");
}
static void ApplyNewProp(PChunkyFile file, PChunkyFile library, const MarcusNamedProp &propEdit)
{
    const auto &definition = propEdit.definition;
    const auto prop = definition.templateNumber, sceneNumber = definition.scene;
    const long frame = definition.first;
    const auto point = definition.point;
    const auto name = propEdit.name.c_str();
    DataBlock block;
    TemplateOnFile tmpl;
    Require(library->FFind(kctgTmpl, prop, &block) && block.FUnpackData() && block.FReadRgb(&tmpl, sizeof(tmpl), 0),
            "Cannot read the selected prop template.");
    if (tmpl.bo == kboOther) SwapBytesBom(&tmpl, kbomTmplf);
    Require(tmpl.bo == kboCur && (tmpl.grftmpl & ftmplProp) && !(tmpl.grftmpl & ftmplTdt),
            "The selected template is not a prop.");
    SceneData scene;
    Require(scene.FRead(file, sceneNumber) && frame >= scene.firstFrame && frame <= scene.lastFrame,
            "The prop insertion frame is outside this scene.");
    ChunkIdentification root;
    const bool hadRoot = file->FGetCkiCtg(kctgMvie, 0, &root);
    if (!hadRoot)
    {
        root.ctg = kctgMvie;
        Require(MovieData::FCreateRoot(file, &root.cno), "Cannot create the movie header.");
        ChunkIdentification existing;
        for (LegacyLong index = 0; file->FGetCkiCtg(kctgScen, index, &existing); ++index)
            Require(file->FAdoptChild(kctgMvie, root.cno, kctgScen, existing.cno, index), "Cannot retain an existing scene.");
    }
    const auto sourceId = RetailSource(file, root.cno, hadRoot ? 2 : 1);
    ChildChunkIdentification rollChild;
    const bool hadRoll = file->FGetKidChidCtg(kctgMvie, root.cno, 0, kctgGst, &rollChild);
    std::unique_ptr<RollCallTable, Release> roll(RollCallTable::New());
    Require(bool(roll), "Cannot create the movie roll call.");
    if (hadRoll)
    {
        int big;
        Require(file->FFind(kctgGst, rollChild.cki.cno, &block), "Cannot read the movie's object names.");
        std::unique_ptr<RollCallTable, Release> wireRoll(RollCallTable::Read(&block, &big));
        Require(wireRoll && wireRoll->CbExtra() == MM_MOVIE_ACTOR_BYTES, "The movie roll call is invalid.");
        for (LegacyLong index = 0; index < wireRoll->IvMac(); ++index)
        {
            unsigned char bytes[MM_MOVIE_ACTOR_BYTES];
            MMMemoryStream memory;
            MMMovieActor actor;
            Require(mm_rollcall_table_extra(&wireRoll->storage, index, bytes) &&
                    mm_memory_stream_init(&memory, bytes, sizeof(bytes)) &&
                    mm_movie_read_actor(&memory.stream, big, &actor, nullptr) == MM_MOVIE_OK,
                    "Cannot decode an existing object name.");
            RollCallActorEntry entry = {};
            entry.arid = actor.actor_id; entry.cactRef = actor.reference_count; entry.grfbrws = actor.browser_flags;
            entry.tagTmpl = {actor.source_id, nullptr, actor.template_type, actor.template_number};
            String existingName;
            wireRoll->GetStn(index, &existingName);
            Require(roll->FAddStn(&existingName, &entry), "Cannot retain an existing object name.");
        }
    }
    LegacyLong maximumId = -1;
    for (LegacyLong index = 0; index < roll->IvMac(); ++index)
    {
        RollCallActorEntry entry;
        roll->GetExtra(index, &entry);
        maximumId = std::max<LegacyLong>(maximumId, entry.arid);
    }
    ChunkIdentification existing;
    for (LegacyLong index = 0; file->FGetCkiCtg(kctgActr, index, &existing); ++index)
    {
        ActorChunkOnFile header;
        Require(file->FFind(kctgActr, existing.cno, &block) && ActorData::FReadHeader(&block, &header),
                "Cannot retain existing actor IDs.");
        maximumId = std::max<LegacyLong>(maximumId, header.arid);
    }
    Require(maximumId < std::numeric_limits<LegacyLong>::max(), "No object IDs are available.");
    ChildChunkID slot = 0;
    ChildChunkIdentification child;
    for (LegacyLong index = 0; file->FGetKid(kctgScen, sceneNumber, index, &child); ++index)
        if (child.cki.ctg == kctgActr)
        {
            Require(child.chid < 0x7fffffffUL, "No scene object slots are available.");
            slot = std::max(slot, child.chid + 1);
        }
    ActorChunkOnFile header = {};
    header.bo = kboCur; header.osk = koskCur;
    header.arid = definition.actorId;
    header.nfrmFirst = static_cast<int32_t>(frame); header.nfrmLast = definition.last;
    header.tagTmpl = {sourceId, 0, kctgTmpl, static_cast<ChunkNumber>(prop)};
    header.dxyzFullRte = {BrFloatToScalar(point[0]), BrFloatToScalar(point[1]), BrFloatToScalar(point[2])};
    ChunkNumber actorNumber, childNumber;
    Require(file->FAdd(sizeof(header), kctgActr, &actorNumber) && ActorData::FWriteHeader(file, actorNumber, &header),
            "Cannot create the prop instance.");
    Require(actorNumber == definition.number || !file->FFind(kctgActr, definition.number), "The new object ID is already in use.");
    if (actorNumber != definition.number) { file->Move(kctgActr, actorNumber, kctgActr, definition.number); actorNumber = definition.number; }
    ActorTrack track;
    Require(track.FCreate(), "Cannot create the prop's stationary track.");
    RouteDistancePoint route = {};
    ActorEvent::Base event = {ActorEvent::aetAdd, static_cast<int32_t>(frame), {0, rZero, 0}};
    ActorEvent::Add addition = {};
    addition.xa = tmpl.xaRest; addition.ya = tmpl.yaRest; addition.za = tmpl.zaRest;
    Require(track.Route()->FInsert(0, &route) && track.Events()->FInsert(0, sizeof(addition), &addition, &event),
            "Cannot place the prop in the scene.");
    ActorEvent::Action action = {0, 0};
    event.aet = ActorEvent::aetActn;
    Require(track.Events()->FInsert(1, sizeof(action), &action, &event), "Cannot set the prop's default pose.");
    event.aet = ActorEvent::aetStep;
    BRS step = rZero;
    Require(track.Events()->FInsert(2, sizeof(step), &step, &event), "Cannot keep the prop stationary.");
    std::unique_ptr<GeneralGroup, Release> wire(ActorData::PggWriteEvents(track.Events()));
    Require(wire && file->FAddChild(kctgActr, actorNumber, 0, track.Route()->CbOnFile(), kctgPath, &childNumber, &block) &&
            track.Route()->FWrite(&block) && file->FAddChild(kctgActr, actorNumber, 0, wire->CbOnFile(), kctgGgae, &childNumber, &block) &&
            wire->FWrite(&block) && file->FAdoptChild(kctgScen, sceneNumber, kctgActr, actorNumber, slot),
            "Cannot write the prop track.");
    Require(file->FGetKidChidCtg(kctgScen, sceneNumber, 1, kctgStartGg, &child) &&
            file->FFind(kctgStartGg, child.cki.cno, &block), "Cannot read the scene's object list.");
    short order;
    std::unique_ptr<GeneralGroup, Release> starts(SceneData::PggReadEvents(&block, &order, true));
    Require(bool(starts), "The scene start events are invalid.");
    SceneEvent start = {scene.firstFrame, sevtAddActr};
    std::uint32_t value = slot;
    if (order == kboOther) { SwapBytesBom(&start, kbomSev); SwapBytesRglw(&value, 1); }
    Require(starts->FInsert(starts->IvMac(), sizeof(value), &value, &start) &&
            file->FAdd(starts->CbOnFile(), kctgStartGg, &childNumber, &block) && starts->FWrite(&block, order),
            "Cannot register the prop in this scene.");
    file->DeleteChild(kctgScen, sceneNumber, kctgStartGg, child.cki.cno, 1);
    Require(file->FAdoptChild(kctgScen, sceneNumber, kctgStartGg, childNumber, 1), "Cannot link the prop to this scene.");
    String propName;
    propName.SetSzs(const_cast<char *>(name));
    RollCallActorEntry entry = {};
    entry.arid = header.arid; entry.cactRef = 1;
    entry.tagTmpl = {sourceId, nullptr, kctgTmpl, static_cast<ChunkNumber>(prop)};
    Require(roll->FAddStn(&propName, &entry), "Cannot name the new prop.");
    if (hadRoll) file->DeleteChild(kctgMvie, root.cno, kctgGst, rollChild.cki.cno, 0);
    Require(MovieData::FWriteRollCall(file, root.cno, roll.get(), &childNumber), "Cannot write the movie's object names.");
    if (frame == scene.firstFrame && file->FGetKidChidCtg(kctgScen, sceneNumber, 0, kctgThumbMbmp, &child))
        file->DeleteChild(kctgScen, sceneNumber, kctgThumbMbmp, child.cki.cno, 0);
}

static void ApplyShape(PChunkyFile file, ChunkNumber actor, const MM::PreviewObjectShape &shape)
{
    const MM::PreviewObjectShape identity;
    const bool rotate = !std::equal(shape.rotation,shape.rotation+9,identity.rotation);
    const bool resize = !std::equal(shape.scale,shape.scale+3,identity.scale);
    if (!rotate && !resize) return;
    ChildChunkIdentification child;
    DataBlock block;
    Require(file->FGetKidChidCtg(kctgActr,actor,0,kctgGgae,&child) && file->FFind(kctgGgae,child.cki.cno,&block), "Cannot read the object transforms.");
    std::unique_ptr<GeneralGroup,Release> events(ActorData::PggReadEvents(&block));
    Require(bool(events),"Cannot decode the object transforms.");
    BMAT34 rotation = {};
    for (int i=0;i<9;++i) rotation.m[i/3][i%3]=BrFloatToScalar(shape.rotation[i]);
    ActorEvent::Stretch stretch={BrFloatToScalar(shape.scale[0]),BrFloatToScalar(shape.scale[1]),BrFloatToScalar(shape.scale[2])};
    for (LegacyLong i=0;i<events->IvMac();++i)
    {
        ActorEvent::Base event;
        events->GetFixed(i,&event);
        if (rotate && (event.aet==ActorEvent::aetRotF || event.aet==ActorEvent::aetRotH))
        {
            BMAT34 original,combined;
            events->Get(i,&original);
            BrMatrix34Mul(&combined,&rotation,&original);
            events->Put(i,&combined);
        }
        if (resize && event.aet==ActorEvent::aetPull)
        {
            ActorEvent::Stretch value;
            events->Get(i,&value);
            value.rScaleX=BrsMul(value.rScaleX,stretch.rScaleX);
            value.rScaleY=BrsMul(value.rScaleY,stretch.rScaleY);
            value.rScaleZ=BrsMul(value.rScaleZ,stretch.rScaleZ);
            events->Put(i,&value);
        }
        if (event.aet==ActorEvent::aetAdd)
        {
            if (rotate)
            {
                event.aet=ActorEvent::aetRotF;
                Require(events->FInsert(++i,sizeof(rotation),&rotation,&event),"Cannot write the object rotation.");
            }
            if (resize)
            {
                event.aet=ActorEvent::aetPull;
                Require(events->FInsert(++i,sizeof(stretch),&stretch,&event),"Cannot write the object size.");
            }
        }
    }
    std::unique_ptr<GeneralGroup,Release> wire(ActorData::PggWriteEvents(events.get()));
    ChunkNumber number;
    Require(wire && file->FAdd(wire->CbOnFile(),kctgGgae,&number,&block) && wire->FWrite(&block),"Cannot write the transformed track.");
    file->DeleteChild(kctgActr,actor,kctgGgae,child.cki.cno,0);
    Require(file->FAdoptChild(kctgActr,actor,kctgGgae,number,0),"Cannot link the transformed track.");
}
std::shared_ptr<MarcusMovieFile> MarcusMaterializeEdits(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, const std::vector<MarcusPropPlacement> &placements,
    const std::vector<MarcusSceneRange> &ranges, const MarcusLiveEdits &edits, const std::filesystem::path &templates)
{
    auto result = WorkingCopy(source, std::move(origin));
    String path;
    Filename filename;
    path.SetSzs(const_cast<char *>(result->path.u8string().c_str()));
    Require(filename.FBuildFromPath(&path), "The object working path is not supported.");
    std::unique_ptr<ChunkyFile, Release> file(source.empty() ? ChunkyFile::PcflCreate(&filename, fcflWriteEnable)
                                                         : ChunkyFile::PcflOpen(&filename, fcflWriteEnable));
    Require(bool(file), "Cannot open the movie working file.");
    ChunkIdentification existing;
    int ordinal = 1;
    while (file->FGetCkiCtg(kctgScen, ordinal - 1, &existing)) ++ordinal;
    for (const auto &scene : *edits.scenes) ApplyNewScene(file.get(), scene, ordinal++, source.empty());
    for (const auto &entry : *edits.storyboards) MarcusEmbedBoard(file.get(), entry.first, entry.second);
    for (const auto &range : ranges) ApplySceneRange(file.get(), range);
    if (!edits.props->empty())
    {
        String templatePath;
        Filename templateFile;
        templatePath.SetSzs(const_cast<char *>(templates.u8string().c_str()));
        Require(templateFile.FBuildFromPath(&templatePath), "The template path is invalid.");
        std::unique_ptr<ChunkyFile, Release> library(ChunkyFile::PcflOpen(&templateFile, fcflNil));
        Require(bool(library), "Cannot open the prop library.");
        for (const auto &prop : *edits.props) ApplyNewProp(file.get(), library.get(), prop);
    }
    for (const auto &placement : placements)
    {
        const auto scene = placement.scene, actor = placement.actor;
        const auto delta = placement.delta;
        ChildChunkIdentification link, child;
        bool found = false;
        for (LegacyLong index = 0; file->FGetKid(kctgScen, scene, index, &link); ++index)
            if (link.cki.ctg == kctgActr && link.cki.cno == actor) { found = true; break; }
        Require(found, "The selected object does not belong to this scene.");
        DataBlock block;
        ActorChunkOnFile header;
        Require(file->FFind(kctgActr, actor, &block) && ActorData::FReadHeader(&block, &header), "Cannot read the object placement.");
        BRS *position[3] = {&header.dxyzFullRte.dxr, &header.dxyzFullRte.dyr, &header.dxyzFullRte.dzr};
        for (int axis = 0; axis < 3; ++axis)
        {
            const double value = double(BrScalarToFloat(*position[axis])) + delta[axis];
            Require(std::isfinite(value) && std::abs(value) <= 32700, "The object placement is out of range.");
            *position[axis] = BrFloatToScalar(static_cast<float>(value));
        }
        // Scene instances can share an ACTR. Copy its root while retaining immutable
        // route, event, template and costume children and the original scene slot.
        ChunkNumber number;
        Require(file->FAdd(sizeof(header), kctgActr, &number) && ActorData::FWriteHeader(file.get(), number, &header),
                "Cannot write the object placement.");
        for (LegacyLong index = 0; file->FGetKid(kctgActr, actor, index, &child); ++index)
            Require(file->FAdoptChild(kctgActr, number, child.cki.ctg, child.cki.cno, child.chid), "Cannot retain the object animation.");
        ApplyShape(file.get(),number,placement.shape);
        file->DeleteChild(kctgScen, scene, kctgActr, actor, link.chid);
        Require(file->FAdoptChild(kctgScen, scene, kctgActr, number, link.chid), "Cannot link the moved object.");
        if (file->FGetKidChidCtg(kctgScen, scene, 0, kctgThumbMbmp, &child))
            file->DeleteChild(kctgScen, scene, kctgThumbMbmp, child.cki.cno, 0);
    }
    for (const auto &entry : *edits.cameras)
    {
        auto frame = entry.first.frame;
        if (frame == std::numeric_limits<std::int32_t>::min())
        {
            SceneData scene;
            Require(scene.FRead(file.get(), entry.first.scene), "Cannot read the opening camera frame.");
            frame = scene.firstFrame;
        }
        ApplyCamera(file.get(), entry.first.scene, frame, entry.second);
    }
    Require(file->FSave(kctgSoc), "Cannot save the object working copy.");
    return result;
}
