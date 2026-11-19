#include "scene_edit.h"
#include "mm/content_vfs.hpp"
#include <algorithm>
#include <exception>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <codecvt>
#include <locale>
#include "soc_core.h"
#include "mm/text.h"
#include "mm/scene_runtime.h"
#include "storyboard_native.h"
#undef size
ASSERTNAME

namespace
{
struct Release { template<class T> void operator()(T *value) const { if (value) value->Release(); } };
void Require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
void AddObjectName(PChunkyFile file, ChunkNumber movie, const ActorChunkOnFile &header,
                   const std::string &name, int flags);
void RegisterSceneObject(PChunkyFile file, ChunkNumber sceneNumber, ChunkNumber actor,
                         LegacyLong firstFrame, bool invalidateThumbnail);
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
static LegacyLong MovieSource(PChunkyFile file, ChunkNumber movie, LegacyLong fallbackSource,
                              const std::string &name)
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
    const auto key = MM::Content::SourceKey(name);
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
        if (id > 0 && MM::Content::SourceKey(text) == key) sourceId = id;
    }
    String sourceName;
    sourceName.SetSzs(PszLit("3D Movie Maker/3DMOVIE"));
    if (!hadSources)
    {
        LegacyLong fallback = fallbackSource;
        Require(sources->FAddStn(&sourceName, &fallback), "Cannot retain original content source.");
        if (key == "3dmovie") sourceId = fallback;
    }
    if (sourceId < 0)
    {
        Require(maxSource < std::numeric_limits<LegacyLong>::max(), "This movie has no available content source IDs.");
        sourceId = maxSource + 1;
        sourceName.SetSzs(const_cast<char *>(name.c_str()));
        Require(sources->FAddStn(&sourceName, &sourceId), "Cannot register the content source.");
    }
    ChunkNumber sourceChunk;
    Require(file->FAdd(sources->CbOnFile(), kctgGst, &sourceChunk, &block) &&
            sources->FWrite(&block), "Cannot write movie content sources.");
    if (hadSources) file->DeleteChild(kctgMvie, movie, kctgGst, child.cki.cno, 1);
    Require(file->FAdoptChild(kctgMvie, movie, kctgGst, sourceChunk, 1), "Cannot link movie content sources.");
    return sourceId;
}
static LegacyLong RetailSource(PChunkyFile file, ChunkNumber movie, LegacyLong fallbackSource)
{ return MovieSource(file, movie, fallbackSource, "3D Movie Maker/3DMOVIE"); }
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
    SceneData scene;
    Require(scene.FRead(file, range.scene), "Cannot read the scene frame range.");
    struct Host
    {
        PChunkyFile file;
        ChunkNumber number;
        SceneData &scene;
        ChildChunkIdentification child;
        DataBlock block;
        short order;
        std::unique_ptr<GeneralGroup, Release> frames;
        const char *message;
        std::exception_ptr failure;
    } host{file, static_cast<ChunkNumber>(range.scene), scene, {}, {}, kboCur, {}, nullptr, {}};
    MMSceneRuntimeServices events = {};
    events.context = &host;
    events.event_count = [](void *p) -> MMI32 { return static_cast<Host *>(p)->frames->IvMac(); };
    events.event = [](void *p, MMI32 index, MMI32 *at, int *kind) {
        auto &host = *static_cast<Host *>(p);
        SceneEvent event;
        host.frames->GetFixed(index, &event);
        if (host.order == kboOther) SwapBytesBom(&event, kbomSev);
        *at = event.nfrm;
        *kind = event.sevt;
    };
    events.move_camera = [](void *p, MMI32 index, MMI32 at) {
        auto &host = *static_cast<Host *>(p);
        SceneEvent event;
        host.frames->GetFixed(index, &event);
        if (host.order == kboOther) SwapBytesBom(&event, kbomSev);
        event.nfrm = at;
        if (host.order == kboOther) SwapBytesBom(&event, kbomSev);
        host.frames->PutFixed(index, &event);
        host.frames->Move(index, 0);
    };
    const MMSceneRangeStoreServices services{
        &events,
        [](void *p) -> int {
            auto &h = *static_cast<Host *>(p);
            try
            {
                h.message = "Cannot read scene frame events.";
                if (!h.file->FGetKidChidCtg(kctgScen, h.number, 0, kctgFrmGg, &h.child) ||
                    !h.file->FFind(kctgFrmGg, h.child.cki.cno, &h.block)) return 0;
                h.message = "Cannot decode scene frame events.";
                h.frames.reset(SceneData::PggReadEvents(&h.block, &h.order, false));
                return bool(h.frames);
            }
            catch (...) { h.failure = std::current_exception(); return 0; }
        },
        [](void *p) -> int {
            auto &h = *static_cast<Host *>(p);
            try
            {
                ChunkNumber number;
                h.message = "Cannot write the extended scene frame events.";
                if (!h.file->FAdd(h.frames->CbOnFile(), kctgFrmGg, &number, &h.block) ||
                    !h.frames->FWrite(&h.block, h.order)) return 0;
                h.message = "Cannot link extended scene frame events.";
                h.file->DeleteChild(kctgScen, h.number, kctgFrmGg, h.child.cki.cno, 0);
                return h.file->FAdoptChild(kctgScen, h.number, kctgFrmGg, number, 0);
            }
            catch (...) { h.failure = std::current_exception(); return 0; }
        },
        [](void *p) -> int {
            auto &h = *static_cast<Host *>(p);
            try
            {
                if (h.file->FGetKidChidCtg(kctgScen, h.number, 0, kctgThumbMbmp, &h.child))
                    h.file->DeleteChild(kctgScen, h.number, kctgThumbMbmp, h.child.cki.cno, 0);
                return 1;
            }
            catch (...) { h.failure = std::current_exception(); return 0; }
        },
        [](void *p) -> int {
            auto &h = *static_cast<Host *>(p);
            try
            {
                h.message = "Cannot write the extended scene.";
                return h.scene.FWrite(h.file, h.number);
            }
            catch (...) { h.failure = std::current_exception(); return 0; }
        }};
    const int status = mm_scene_range_store(&scene, range.first, range.last, &services);
    if (host.failure)
        std::rethrow_exception(host.failure);
    Require(status != MM_SCENE_RANGE_INVALID, "The scene range must retain its original frames.");
    Require(status != 0, host.message);
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
    Require(tmpl.bo == kboCur && bool(tmpl.grftmpl & ftmplTdt) == bool(propEdit.file),
            "The selected object template is invalid.");
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
    ChunkNumber templateNumber = prop;
    if (propEdit.file)
    {
        Require(library->FCopy(kctgTmpl, prop, file, &templateNumber), "Cannot embed the 3D text template.");
        ChildChunkIdentification textChild;
        Require(file->FGetKidChidCtg(kctgTmpl, templateNumber, 0, kctgTdt, &textChild) &&
                file->FFind(kctgTdt, textChild.cki.cno, &block) && block.FUnpackData(), "Cannot read the 3D text font reference.");
        // Generated text is embedded; its font remains original retail content.
        std::int32_t fontSource = sourceId;
        Require(block.FWriteRgb(&fontSource, sizeof(fontSource), 8), "Cannot retain the original 3D font.");
    }
    ActorChunkOnFile header = {};
    header.bo = kboCur; header.osk = koskCur;
    header.arid = definition.actorId;
    header.nfrmFirst = static_cast<int32_t>(frame); header.nfrmLast = definition.last;
    header.tagTmpl = {propEdit.file ? 0 : sourceId, 0, kctgTmpl, templateNumber};
    header.dxyzFullRte = {BrFloatToScalar(point[0]), BrFloatToScalar(point[1]), BrFloatToScalar(point[2])};
    ChunkNumber actorNumber, childNumber;
    Require(file->FAdd(sizeof(header), kctgActr, &actorNumber) && ActorData::FWriteHeader(file, actorNumber, &header),
            "Cannot create the prop instance.");
    Require(actorNumber == definition.number || !file->FFind(kctgActr, definition.number), "The new object ID is already in use.");
    if (actorNumber != definition.number) { file->Move(kctgActr, actorNumber, kctgActr, definition.number); actorNumber = definition.number; }
    if (propEdit.file)
        Require(file->FAdoptChild(kctgActr, actorNumber, kctgTmpl, templateNumber, 0), "Cannot link the embedded 3D text.");
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
            wire->FWrite(&block), "Cannot write the prop track.");
    RegisterSceneObject(file, sceneNumber, actorNumber, scene.firstFrame, frame == scene.firstFrame);
    AddObjectName(file, root.cno, header, propEdit.file ? MarcusTextAnsi(name) : std::string(name),
                  propEdit.file ? 2 : (tmpl.grftmpl & ftmplProp) ? 1 : 0);
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
std::string MarcusTextAnsi(const char *utf8)
{
    Require(utf8 && *utf8, "Enter some text first.");
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    const auto wide = convert.from_bytes(utf8);
    Require(!wide.empty() && wide.size() <= 50 && wide.find_first_not_of(u" \t\r\n") != std::u16string::npos,
        "3D text needs 1 to 50 characters, including at least one visible character.");
    const auto bytes = mm_text_ansi(nullptr, wide.data(), wide.size() * 2, MM_TEXT_UTF16_LE, nullptr, 0);
    std::string result(bytes, 0);
    Require(bytes && bytes <= 50 && mm_text_ansi(nullptr, wide.data(), wide.size() * 2, MM_TEXT_UTF16_LE, result.data(), bytes) == bytes,
        "This text cannot be used with the original 3D fonts.");
    std::vector<std::uint16_t> roundTrip(wide.size());
    const auto units = mm_text_utf16(nullptr, result.data(), result.size(), MM_TEXT_WINDOWS, roundTrip.data(), roundTrip.size());
    Require(units == wide.size() && std::equal(wide.begin(), wide.end(), roundTrip.begin()) &&
        result.find_first_of("\r\n\t") == std::string::npos, "Use a single line of characters supported by the original 3D fonts.");
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusCreateTextTemplate(const char *utf8, unsigned long font, long source, int shape)
{
    const auto ansi = MarcusTextAnsi(utf8);
    Require(shape >= 0 && shape < tdtsLim, "Choose a valid 3D text shape.");
    auto result = WorkingCopy({}, nullptr);
    String path, text; Filename filename;
    path.SetSzs(const_cast<char *>(result->path.u8string().c_str()));
    text.SetSzs(const_cast<char *>(ansi.c_str()));
    Require(filename.FBuildFromPath(&path), "Invalid 3D text working path.");
    std::unique_ptr<ChunkyFile, Release> file(ChunkyFile::PcflCreate(&filename, fcflWriteEnable));
    TAG tag = {source, nullptr, kctgTdf, font};
    std::unique_ptr<ThreeDText, Release> object(ThreeDText::PtdtNew(&text, shape, &tag));
    ChunkNumber number;
    Require(file && object && object->FWrite(file.get(), kctgTmpl, &number) && number == 0 && file->FSave(kctgSoc),
        "Cannot create the 3D text template.");
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusCreateEventFile() { return WorkingCopy({}, nullptr); }
std::shared_ptr<MarcusMovieFile> MarcusCopyMovie(const std::shared_ptr<MarcusMovieFile> &source)
{ return WorkingCopy(source->path, source); }

namespace
{
Filename ObjectFile(const std::filesystem::path &path)
{
    String text;
    text.SetSzs(const_cast<char *>(path.u8string().c_str()));
    Filename name;
    Require(name.FBuildFromPath(&text), "Invalid object clipboard path.");
    return name;
}

ChildChunkID FreeChildSlot(PChunkyFile file, ChunkTagOrType parent, ChunkNumber number, ChunkTagOrType type)
{
    ChildChunkIdentification child;
    ChildChunkID slot = 0;
    for (LegacyLong i = 0; file->FGetKid(parent, number, i, &child); ++i)
        if (child.cki.ctg == type)
        {
            Require(child.chid < 0x7fffffffUL, "No available child slots for the pasted object.");
            slot = std::max(slot, child.chid + 1);
        }
    return slot;
}

void AddObjectName(PChunkyFile file, ChunkNumber movie, const ActorChunkOnFile &header,
                   const std::string &name, int flags)
{
    ChildChunkIdentification child;
    DataBlock block;
    const bool hadRoll = file->FGetKidChidCtg(kctgMvie, movie, 0, kctgGst, &child);
    std::unique_ptr<RollCallTable, Release> roll(RollCallTable::New());
    Require(bool(roll), "Cannot allocate the movie roll call.");
    if (hadRoll)
    {
        int big;
        Require(file->FFind(kctgGst, child.cki.cno, &block), "Cannot read the movie roll call.");
        std::unique_ptr<RollCallTable, Release> wire(RollCallTable::Read(&block, &big));
        Require(wire && wire->CbExtra() == MM_MOVIE_ACTOR_BYTES, "The movie roll call is invalid.");
        for (LegacyLong i = 0; i < wire->IvMac(); ++i)
        {
            unsigned char bytes[MM_MOVIE_ACTOR_BYTES];
            MMMemoryStream stream;
            MMMovieActor actor;
            Require(mm_rollcall_table_extra(&wire->storage, i, bytes) &&
                    mm_memory_stream_init(&stream, bytes, sizeof(bytes)) &&
                    mm_movie_read_actor(&stream.stream, big, &actor, nullptr) == MM_MOVIE_OK,
                    "Cannot decode the movie roll call.");
            RollCallActorEntry entry = {};
            entry.arid = actor.actor_id; entry.cactRef = actor.reference_count; entry.grfbrws = actor.browser_flags;
            entry.tagTmpl = {actor.source_id, nullptr, actor.template_type, actor.template_number};
            String previous;
            wire->GetStn(i, &previous);
            Require(roll->FAddStn(&previous, &entry), "Cannot retain an existing object name.");
        }
    }
    String caption;
    caption.SetSzs(const_cast<char *>(name.c_str()));
    RollCallActorEntry entry = {};
    entry.arid = header.arid; entry.cactRef = 1; entry.grfbrws = flags;
    entry.tagTmpl = {header.tagTmpl.sid, nullptr, header.tagTmpl.ctg, header.tagTmpl.cno};
    Require(roll->FAddStn(&caption, &entry), "Cannot name the pasted object.");
    if (hadRoll) file->DeleteChild(kctgMvie, movie, kctgGst, child.cki.cno, 0);
    ChunkNumber rollNumber;
    Require(MovieData::FWriteRollCall(file, movie, roll.get(), &rollNumber), "Cannot write the movie roll call.");
}

void RegisterSceneObject(PChunkyFile file, ChunkNumber sceneNumber, ChunkNumber actor,
                         LegacyLong firstFrame, bool invalidateThumbnail)
{
    const auto slot = FreeChildSlot(file, kctgScen, sceneNumber, kctgActr);
    ChildChunkIdentification child;
    DataBlock block;
    Require(file->FGetKidChidCtg(kctgScen, sceneNumber, 1, kctgStartGg, &child) &&
            file->FFind(kctgStartGg, child.cki.cno, &block), "Cannot read the scene's object list.");
    short order;
    std::unique_ptr<GeneralGroup, Release> starts(SceneData::PggReadEvents(&block, &order, true));
    Require(bool(starts), "The scene start events are invalid.");
    SceneEvent start = {firstFrame, sevtAddActr};
    auto value = slot;
    if (order == kboOther) { SwapBytesBom(&start, kbomSev); SwapBytesRglw(&value, 1); }
    ChunkNumber number;
    Require(starts->FInsert(starts->IvMac(), sizeof(value), &value, &start) &&
            file->FAdd(starts->CbOnFile(), kctgStartGg, &number, &block) && starts->FWrite(&block, order) &&
            file->FAdoptChild(kctgScen, sceneNumber, kctgActr, actor, slot), "Cannot register the pasted object.");
    file->DeleteChild(kctgScen, sceneNumber, kctgStartGg, child.cki.cno, 1);
    Require(file->FAdoptChild(kctgScen, sceneNumber, kctgStartGg, number, 1), "Cannot link scene start events.");
    if (invalidateThumbnail && file->FGetKidChidCtg(kctgScen, sceneNumber, 0, kctgThumbMbmp, &child))
        file->DeleteChild(kctgScen, sceneNumber, kctgThumbMbmp, child.cki.cno, 0);
}

std::int32_t ShiftFrame(std::int32_t frame, std::int64_t delta)
{
    if (frame == knfrmInvalid) return frame;
    const auto shifted = frame + delta;
    Require(shifted >= std::numeric_limits<std::int32_t>::min() && shifted < knfrmInvalid,
            "The pasted object track would exceed the frame range.");
    return static_cast<std::int32_t>(shifted);
}

ChunkNumber TransferObject(PChunkyFile source, ChunkNumber actor, ChunkNumber sourceScene,
    PChunkyFile destination, ChunkNumber destinationScene, std::int32_t actorId, std::int64_t delta,
    const std::function<LegacyLong(LegacyLong)> &sourceId, ActorChunkOnFile &header)
{
    ChunkNumber copied;
    Require(ActorData::FReadHeader(source, actor, &header) &&
            source->FClone(kctgActr, actor, destination, &copied), "Cannot copy the object's native track.");
    const auto local = [&](ChunkTagOrType type, ChunkNumber number) {
        ChunkNumber result;
        Require(source->FFind(type, number) && source->FCopy(type, number, destination, &result),
                "The copied object is missing an embedded asset.");
        return result;
    };
    const auto retarget = [&](auto &tag, ChunkNumber owner, ChunkTagOrType ownerType) {
        if (tag.sid == 0)
        {
            tag.cno = local(tag.ctg, tag.cno);
            const auto slot = FreeChildSlot(destination, ownerType, owner, tag.ctg);
            Require(destination->FAdoptChild(ownerType, owner, tag.ctg, tag.cno, slot),
                    "Cannot retain the object's embedded asset.");
        }
        else if (tag.sid > 0) tag.sid = sourceId(tag.sid);
    };
    header.arid = actorId;
    header.nfrmFirst = ShiftFrame(header.nfrmFirst, delta);
    header.nfrmLast = ShiftFrame(header.nfrmLast, delta);
    ChildChunkIdentification child, text;
    DataBlock block;
    if (header.tagTmpl.sid == 0)
    {
        Require(destination->FGetKidChidCtg(kctgActr, copied, 0, kctgTmpl, &child), "The embedded template is missing.");
        header.tagTmpl.cno = child.cki.cno;
        if (destination->FGetKidChidCtg(kctgTmpl, child.cki.cno, 0, kctgTdt, &text))
        {
            unsigned char descriptor[24];
            Require(destination->FFind(kctgTdt, text.cki.cno, &block) && block.FUnpackData() &&
                    block.Cb() == sizeof(descriptor) && block.FReadRgb(descriptor, sizeof(descriptor), 0),
                    "Cannot read the copied 3D text font.");
            short order;
            TAGOnFile font;
            std::memcpy(&order, descriptor, sizeof(order));
            Require(order == kboCur || order == kboOther, "Invalid copied 3D text font.");
            std::memcpy(&font, descriptor + 8, sizeof(font));
            if (order == kboOther) SwapBytesRglw(&font, 4);
            retarget(font, child.cki.cno, kctgTmpl);
            if (order == kboOther) SwapBytesRglw(&font, 4);
            std::memcpy(descriptor + 8, &font, sizeof(font));
            Require(destination->FPutPv(descriptor, sizeof(descriptor), kctgTdt, text.cki.cno),
                    "Cannot write the copied 3D text font.");
        }
    }
    else if (header.tagTmpl.sid > 0) header.tagTmpl.sid = sourceId(header.tagTmpl.sid);
    Require(ActorData::FWriteHeader(destination, copied, &header), "Cannot write the pasted object ID.");

    std::unique_ptr<GeneralGroup, Release> events(ActorData::PggReadEvents(source, actor));
    Require(bool(events), "Cannot read the object's animation events.");
    std::map<ChunkNumber, std::pair<ChunkNumber, ChildChunkID>> sounds;
    for (LegacyLong i = 0; i < events->IvMac(); ++i)
    {
        ActorEvent::Base event;
        events->GetFixed(i, &event);
        event.nfrm = ShiftFrame(event.nfrm, delta);
        events->PutFixed(i, &event);
        if (event.aet == ActorEvent::aetCost)
        {
            ActorEvent::Costume costume;
            events->Get(i, &costume);
            // Custom costumes are slots of the actor's template.
            if (!costume.fCmtl)
            {
                retarget(costume.tag, copied, kctgActr);
                events->Put(i, &costume);
            }
        }
        else if (event.aet == ActorEvent::aetSnd)
        {
            ActorEvent::Sound sound;
            events->Get(i, &sound);
            if (sound.fNoSound) continue;
            if (sound.tag.sid == 0)
            {
                const auto number = source->FGetKidChidCtg(kctgScen, sourceScene, sound.chid, kctgMsnd, &child)
                    ? child.cki.cno : sound.tag.cno;
                auto found = sounds.find(number);
                if (found == sounds.end())
                {
                    const auto asset = local(kctgMsnd, number);
                    const auto slot = FreeChildSlot(destination, kctgScen, destinationScene, kctgMsnd);
                    Require(destination->FAdoptChild(kctgScen, destinationScene, kctgMsnd, asset, slot),
                            "Cannot link the copied actor sound.");
                    found = sounds.emplace(number, std::make_pair(asset, slot)).first;
                }
                sound.tag.cno = found->second.first;
                sound.chid = found->second.second;
            }
            else if (sound.tag.sid > 0) sound.tag.sid = sourceId(sound.tag.sid);
            events->Put(i, &sound);
        }
    }
    std::unique_ptr<GeneralGroup, Release> wire(ActorData::PggWriteEvents(events.get()));
    Require(wire && destination->FGetKidChidCtg(kctgActr, copied, 0, kctgGgae, &child) &&
            destination->FPut(wire->CbOnFile(), kctgGgae, child.cki.cno, &block) && wire->FWrite(&block),
            "Cannot write the pasted animation events.");
    return copied;
}
}

std::unique_ptr<MarcusObjectClipboard> MarcusCopyObject(const std::filesystem::path &movie, unsigned long scene,
    long frame, const MarcusObject &object)
{
    MM::Archive archive;
    MM::MovieDirectory catalogue;
    MM::Error error;
    if (!archive.Open(movie.u8string(), error) || !archive.ReadMovieDirectory(catalogue, error))
        throw std::runtime_error(error.message);
    auto result = std::make_unique<MarcusObjectClipboard>();
    result->sources = std::move(catalogue.sources);
    if (!catalogue.hasRoot) result->sources.push_back({1, "3D Movie Maker/3DMOVIE"});
    result->frame = frame;
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    const auto wide = convert.from_bytes(object.name);
    result->name.resize(mm_text_ansi(nullptr, wide.data(), wide.size() * 2, MM_TEXT_UTF16_LE, nullptr, 0));
    Require(result->name.empty() || mm_text_ansi(nullptr, wide.data(), wide.size() * 2, MM_TEXT_UTF16_LE,
            result->name.data(), result->name.size()) == result->name.size(), "Cannot encode the object name.");
    result->browserFlags = object.text ? 2 : object.prop ? 1 : 0;
    result->file = MarcusCreateEventFile();
    auto original = ObjectFile(movie), clipboard = ObjectFile(result->file->path);
    std::unique_ptr<ChunkyFile, Release> source(ChunkyFile::PcflOpen(&original, fcflNil));
    std::unique_ptr<ChunkyFile, Release> destination(ChunkyFile::PcflCreate(&clipboard, fcflWriteEnable));
    Require(source && destination && destination->FAdd(0, kctgScen, &result->sounds), "Cannot create the object clipboard.");
    ChildChunkIdentification child;
    ActorChunkOnFile header;
    for (LegacyLong i = 0; source->FGetKid(kctgScen, scene, i, &child); ++i)
        if (child.cki.ctg == kctgActr && ActorData::FReadHeader(source.get(), child.cki.cno, &header) &&
            header.arid == object.actor_id)
        {
            result->actor = TransferObject(source.get(), child.cki.cno, scene, destination.get(), result->sounds,
                header.arid, 0, [](LegacyLong id) { return id; }, header);
            Require(destination->FSave(kctgSoc), "Cannot save the object clipboard.");
            return result;
        }
    throw std::runtime_error("Cannot find the selected object in the movie.");
}

std::shared_ptr<MarcusMovieFile> MarcusPasteObject(const std::filesystem::path &movie,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusObjectClipboard &clipboard,
    unsigned long sceneNumber, long frame, std::int32_t actorId)
{
    auto result = WorkingCopy(movie, origin);
    auto sourceName = ObjectFile(clipboard.file->path), destinationName = ObjectFile(result->path);
    std::unique_ptr<ChunkyFile, Release> source(ChunkyFile::PcflOpen(&sourceName, fcflNil));
    std::unique_ptr<ChunkyFile, Release> destination(ChunkyFile::PcflOpen(&destinationName, fcflWriteEnable));
    Require(source && destination, "Cannot open the movie or object working copy.");
    SceneData scene;
    Require(scene.FRead(destination.get(), sceneNumber) && frame >= scene.firstFrame && frame <= scene.lastFrame,
            "The paste frame is outside the scene.");
    ChunkIdentification root;
    const bool hadRoot = destination->FGetCkiCtg(kctgMvie, 0, &root);
    if (!hadRoot)
    {
        Require(MovieData::FCreateRoot(destination.get(), &root.cno), "Cannot create the movie header.");
        ChunkIdentification existing;
        for (LegacyLong i = 0; destination->FGetCkiCtg(kctgScen, i, &existing); ++i)
            Require(destination->FAdoptChild(kctgMvie, root.cno, kctgScen, existing.cno, i), "Cannot retain scene order.");
    }
    std::map<LegacyLong, LegacyLong> ids;
    const auto sourceId = [&](LegacyLong id) {
        const auto found = ids.find(id);
        if (found != ids.end()) return found->second;
        const auto entry = std::find_if(clipboard.sources.begin(), clipboard.sources.end(),
            [id](const auto &value) { return value.id == id; });
        Require(entry != clipboard.sources.end(), "The copied object uses an unknown content source.");
        const auto mapped = MovieSource(destination.get(), root.cno, hadRoot ? 2 : 1, entry->name);
        ids.emplace(id, mapped);
        return mapped;
    };
    // Headless scene-only movies rely on an implicit retail source.
    if (!hadRoot) RetailSource(destination.get(), root.cno, 1);
    ActorChunkOnFile header;
    const auto actor = TransferObject(source.get(), clipboard.actor, clipboard.sounds, destination.get(),
        sceneNumber, actorId, std::int64_t(frame) - clipboard.frame, sourceId, header);
    RegisterSceneObject(destination.get(), sceneNumber, actor, scene.firstFrame, true);
    AddObjectName(destination.get(), root.cno, header, clipboard.name, clipboard.browserFlags);
    Require(destination->FSave(kctgSoc), "Cannot save the pasted object.");
    return result;
}

namespace
{
struct SceneReframe
{
    double linear[3][3] = {}, translation[3] = {};
    BMAT34 rotation = {};
};

BRS ReframeScalar(double value)
{
    Require(std::isfinite(value) && std::abs(value) <= 32700, "The camera transform would move an object out of range.");
    return BrFloatToScalar(static_cast<float>(value));
}

SceneReframe CameraReframe(const MarcusProjection &work, const MarcusProjection &camera)
{
    const double radians = .008726646259971648;
    const double magnification = std::tan(camera.vertical_fov * radians) / std::tan(work.vertical_fov * radians);
    const double scale[] = {magnification * camera.aspect / work.aspect, magnification, 1};
    Require(std::isfinite(scale[0]) && std::isfinite(scale[1]) && scale[0] > 0 && scale[1] > 0,
            "The work and movie cameras need valid perspective lenses.");
    SceneReframe result;
    for (int col = 0; col < 3; ++col)
    {
        result.translation[col] = camera.transform[9 + col];
        for (int row = 0; row < 3; ++row)
        {
            double rotation = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double component = work.transform[axis * 3 + row] * double(camera.transform[axis * 3 + col]);
                result.linear[row][col] += component * scale[axis];
                rotation += component;
            }
            result.rotation.m[row][col] = ReframeScalar(rotation);
            result.translation[col] -= work.transform[9 + row] * result.linear[row][col];
        }
    }
    return result;
}

RoutePoint ReframePoint(const SceneReframe &change, const RoutePoint &point, const RoutePoint &anchor = {})
{
    const BRS input[] = {point.dxr, point.dyr, point.dzr};
    const BRS origin[] = {anchor.dxr, anchor.dyr, anchor.dzr};
    BRS output[3];
    for (int col = 0; col < 3; ++col)
    {
        double value = BrScalarToFloat(origin[col]);
        for (int i = 0; i < 3; ++i)
            value += (double(BrScalarToFloat(input[i])) - BrScalarToFloat(origin[i])) * change.linear[i][col];
        output[col] = ReframeScalar(value);
    }
    return {output[0], output[1], output[2]};
}

// Delta for an unchanged point whose world-space position is translation + point.
RoutePoint ReframeDisplacement(const SceneReframe &change, const RoutePoint &translation, const RoutePoint &point)
{
    const double world[] = {double(BrScalarToFloat(translation.dxr)) + BrScalarToFloat(point.dxr),
                            double(BrScalarToFloat(translation.dyr)) + BrScalarToFloat(point.dyr),
                            double(BrScalarToFloat(translation.dzr)) + BrScalarToFloat(point.dzr)};
    BRS output[3];
    for (int col = 0; col < 3; ++col)
    {
        double value = change.translation[col] - world[col];
        for (int i = 0; i < 3; ++i) value += world[i] * change.linear[i][col];
        output[col] = ReframeScalar(value);
    }
    return {output[0], output[1], output[2]};
}

BMAT34 ReframeRotation(const SceneReframe &change, const BMAT34 &before)
{
    BMAT34 result;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 3; ++col)
        {
            double value = 0;
            for (int i = 0; i < 3; ++i) value += BrScalarToFloat(before.m[row][i]) * change.linear[i][col];
            result.m[row][col] = ReframeScalar(value);
        }
    return result;
}

LegacyLong NextEntrance(PGeneralGroup events, LegacyLong i)
{
    for (++i; i < events->IvMac(); ++i)
    {
        ActorEvent::Base event;
        events->GetFixed(i, &event);
        if (event.aet == ActorEvent::aetAdd) break;
    }
    return i;
}

// Split the current segment at its evaluated point so earlier route points
// remain in the original coordinate system. Rebase the locations of events.
RoutePoint SplitReframeSegment(PDynamicArray route, PGeneralGroup events, RouteLocation &cut)
{
    Require(cut.irpt >= 0 && cut.irpt < route->IvMac(), "Invalid object position on its track.");
    RouteDistancePoint point;
    route->Get(cut.irpt, &point);
    if (!cut.dwrOffset) return point.xyz;
    Require(cut.dwrOffset > 0 && cut.dwrOffset <= point.dwr && cut.irpt + 1 < route->IvMac(),
            "Invalid fractional position on an object track.");
    RouteDistancePoint next;
    route->Get(cut.irpt + 1, &next);
    const auto old = cut;
    const bool inserted = cut.dwrOffset < point.dwr;
    RoutePoint pivot = next.xyz;
    if (inserted)
    {
        mm_actor_interpolate(&point.xyz, &next.xyz, mm_fixed_div(cut.dwrOffset, point.dwr), &pivot);
        RouteDistancePoint split = {pivot, point.dwr - cut.dwrOffset};
        Require(route->FInsert(cut.irpt + 1, &split), "Cannot split the object track at the copied frame.");
        point.dwr = cut.dwrOffset;
        route->Put(cut.irpt, &point);
    }
    for (LegacyLong i = 0; i < events->IvMac(); ++i)
    {
        ActorEvent::Base event;
        events->GetFixed(i, &event);
        if (event.rtel.irpt == old.irpt && event.rtel.dwrOffset >= old.dwrOffset)
        {
            ++event.rtel.irpt;
            event.rtel.dwrOffset -= old.dwrOffset;
        }
        else if (inserted && event.rtel.irpt > old.irpt) ++event.rtel.irpt;
        else continue;
        events->PutFixed(i, &event);
    }
    ++cut.irpt;
    cut.dwrOffset = 0;
    return pivot;
}

void ReframeTrack(PChunkyFile file, ChunkNumber actor, ChunkNumber copy, const ActorChunkOnFile &header,
                  const SceneReframe &change, const MM::PreviewTrackPose *pose, LegacyLong frame, bool fixedFacing)
{
    std::unique_ptr<DynamicArray, Release> route(ActorData::PglReadRoute(file, actor));
    std::unique_ptr<GeneralGroup, Release> events(ActorData::PggReadEvents(file, actor));
    Require(route && events, "Cannot read an object track for the camera transform.");
    RouteLocation cut = {};
    RoutePoint pivot = {};
    LegacyLong active = -1;
    if (pose)
    {
        cut = {pose->node, pose->offset, pose->routeFrame};
        for (LegacyLong i = 0; i < events->IvMac(); ++i)
        {
            ActorEvent::Base event;
            events->GetFixed(i, &event);
            if (event.aet == ActorEvent::aetAdd && event.nfrm <= frame && event.rtel <= cut)
                active = i;
        }
        Require(active >= 0, "Cannot locate the visible object's entrance.");
        ActorEvent::Base entry;
        events->GetFixed(active, &entry);
        if (entry.nfrm < frame) pivot = SplitReframeSegment(route.get(), events.get(), cut);
    }

    for (LegacyLong start = 0; start < events->IvMac();)
    {
        ActorEvent::Base entry;
        events->GetFixed(start, &entry);
        Require(entry.aet == ActorEvent::aetAdd && entry.rtel.irpt >= 0 && entry.rtel.irpt < route->IvMac(),
                "Invalid object entrance.");
        LegacyLong end = NextEntrance(events.get(), start);
        const bool partial = start == active && entry.nfrm < frame;
        if (!partial && entry.nfrm < frame)
        {
            start = end;
            continue;
        }
        const auto firstNode = partial ? cut.irpt + 1 : entry.rtel.irpt;
        auto endNode = route->IvMac();
        if (end < events->IvMac())
        {
            ActorEvent::Base next;
            events->GetFixed(end, &next);
            endNode = next.rtel.irpt;
        }
        Require(endNode >= firstNode && endNode <= route->IvMac(), "Invalid object subroute.");
        for (LegacyLong i = firstNode; i < endNode; ++i)
        {
            RouteDistancePoint point;
            route->Get(i, &point);
            point.xyz = ReframePoint(change, point.xyz, partial ? pivot : RoutePoint{});
            // Preserve the original interpolation distances and event timing.
            route->Put(i, &point);
        }

        ActorEvent::Add add;
        events->Get(start, &add);
        br_euler angles = {add.xa, add.ya, add.za, BR_EULER_XYZ_S};
        BMAT34 entrance, inverseEntrance;
        BrEulerToMatrix34(&entrance, &angles);
        BrMatrix34LPInverse(&inverseEntrance, &entrance);
        RouteDistancePoint first;
        route->Get(entry.rtel.irpt, &first);
        const bool followsPath = !fixedFacing && first.dwr != rZero;
        if (!partial)
        {
            const auto delta = ReframeDisplacement(change, header.dxyzFullRte, {add.dxr, add.dyr, add.dzr});
            add.dxr = ReframeScalar(double(BrScalarToFloat(add.dxr)) + BrScalarToFloat(delta.dxr));
            add.dyr = ReframeScalar(double(BrScalarToFloat(add.dyr)) + BrScalarToFloat(delta.dyr));
            add.dzr = ReframeScalar(double(BrScalarToFloat(add.dzr)) + BrScalarToFloat(delta.dzr));
            if (followsPath)
            {
                BMAT34 transformed, rotation = change.rotation;
                BrMatrix34Mul(&transformed, &entrance, &rotation);
                BrMatrix34ToEuler(&angles, &transformed);
                add.xa = angles.a; add.ya = angles.b; add.za = angles.c;
            }
            events->Put(start, &add);
        }

        LegacyLong at = start + 1;
        if (!partial && !followsPath)
        {
            BMAT34 transformed = ReframeRotation(change, entrance), basis;
            BrMatrix34Mul(&basis, &transformed, &inverseEntrance);
            auto event = entry;
            event.aet = ActorEvent::aetRotF;
            Require(events->FInsert(at++, sizeof(basis), &basis, &event), "Cannot rotate the object's entrance.");
            ++end;
        }

        // At the active route location, retain events already evaluated and
        // apply the transform after them. Later events use the new coordinates.
        LegacyLong insert = at;
        for (; at < end; ++at)
        {
            ActorEvent::Base event;
            events->GetFixed(at, &event);
            if (start == active && event.rtel <= cut) insert = at + 1;
            if (partial && event.rtel <= cut) continue;
            if (event.aet == ActorEvent::aetMove || event.aet == ActorEvent::aetTweak)
            {
                RoutePoint point;
                events->Get(at, &point);
                point = ReframePoint(change, point, partial && event.aet == ActorEvent::aetTweak ? pivot : RoutePoint{});
                events->Put(at, &point);
            }
            else if (event.aet == ActorEvent::aetRotH || (event.aet == ActorEvent::aetRotF && !followsPath))
            {
                BMAT34 before;
                events->Get(at, &before);
                if (event.aet == ActorEvent::aetRotF)
                {
                    BMAT34 withEntrance;
                    BrMatrix34Mul(&withEntrance, &before, &entrance);
                    BMAT34 transformed = ReframeRotation(change, withEntrance);
                    BrMatrix34Mul(&before, &transformed, &inverseEntrance);
                }
                else before = ReframeRotation(change, before);
                events->Put(at, &before);
            }
        }
        if (start == active)
        {
            if (partial)
            {
                const RoutePoint translation = {pose->translation[0], pose->translation[1], pose->translation[2]};
                auto delta = ReframeDisplacement(change, translation, pivot);
                ActorEvent::Base move = {ActorEvent::aetMove, frame, cut};
                Require(events->FInsert(insert++, sizeof(delta), &delta, &move), "Cannot move the object at the copied frame.");
                ++end;
                const RoutePoint evaluated = {pose->position[0], pose->position[1], pose->position[2]};
                if (evaluated != pivot)
                {
                    auto point = ReframePoint(change, evaluated, pivot);
                    move.aet = ActorEvent::aetTweak;
                    Require(events->FInsert(insert++, sizeof(point), &point, &move), "Cannot retain the object's exact position.");
                    ++end;
                }
            }
            BMAT34 before = {};
            for (int i = 0; i < 9; ++i) before.m[i / 3][i % 3] = ReframeScalar(pose->rotation[i]);
            auto rotation = ReframeRotation(change, before);
            ActorEvent::Base turn = {ActorEvent::aetRotH, frame, cut};
            Require(events->FInsert(insert, sizeof(rotation), &rotation, &turn), "Cannot preserve the copied object facing.");
            ++end;
        }
        start = end;
    }

    std::unique_ptr<GeneralGroup, Release> wire(ActorData::PggWriteEvents(events.get()));
    ChunkNumber number;
    DataBlock block;
    Require(wire && file->FAddChild(kctgActr, copy, 0, route->CbOnFile(), kctgPath, &number, &block) &&
            route->FWrite(&block) && file->FAddChild(kctgActr, copy, 0, wire->CbOnFile(), kctgGgae, &number, &block) &&
            wire->FWrite(&block), "Cannot write the reframed object track.");
}

}

std::shared_ptr<MarcusMovieFile> MarcusReframeScene(const std::filesystem::path &movie,
    const std::shared_ptr<MarcusMovieFile> &origin, unsigned long scene, long frame,
    const MarcusProjection &work, const MarcusProjection &camera,
    const std::map<std::int32_t, MM::PreviewTrackPose> &poses)
{
    const auto change = CameraReframe(work, camera);
    auto result = WorkingCopy(movie, origin);
    auto path = ObjectFile(result->path);
    std::unique_ptr<ChunkyFile, Release> file(ChunkyFile::PcflOpen(&path, fcflWriteEnable));
    Require(bool(file), "Cannot open the camera transform working copy.");
    std::vector<ChildChunkIdentification> actors;
    ChildChunkIdentification child;
    for (LegacyLong i = 0; file->FGetKid(kctgScen, scene, i, &child); ++i)
        if (child.cki.ctg == kctgActr) actors.push_back(child);
    Require(!actors.empty(), "This scene has no objects to transform.");
    for (const auto &link : actors)
    {
        ActorChunkOnFile header;
        const auto actor = link.cki.cno;
        Require(ActorData::FReadHeader(file.get(), actor, &header), "Cannot read an object for the camera transform.");
        ChunkNumber copy;
        Require(file->FAdd(sizeof(header), kctgActr, &copy) && ActorData::FWriteHeader(file.get(), copy, &header),
                "Cannot write the new object placement.");
        for (LegacyLong i = 0; file->FGetKid(kctgActr, actor, i, &child); ++i)
            if (child.cki.ctg != kctgPath && child.cki.ctg != kctgGgae)
                Require(file->FAdoptChild(kctgActr, copy, child.cki.ctg, child.cki.cno, child.chid),
                        "Cannot retain an object's content.");
        const auto pose = poses.find(header.arid);
        ChildChunkIdentification model, text;
        const bool fixedFacing = header.tagTmpl.sid == 0 && file->FGetKidChidCtg(kctgActr, actor, 0, kctgTmpl, &model) &&
            file->FGetKidChidCtg(kctgTmpl, model.cki.cno, 0, kctgTdt, &text);
        ReframeTrack(file.get(), actor, copy, header, change, pose == poses.end() ? nullptr : &pose->second,
                     static_cast<LegacyLong>(frame), fixedFacing);
        file->DeleteChild(kctgScen, scene, kctgActr, actor, link.chid);
        Require(file->FAdoptChild(kctgScen, scene, kctgActr, copy, link.chid), "Cannot link the reframed object.");
    }
    if (file->FGetKidChidCtg(kctgScen, scene, 0, kctgThumbMbmp, &child))
        file->DeleteChild(kctgScen, scene, kctgThumbMbmp, child.cki.cno, 0);
    Require(file->FSave(kctgSoc), "Cannot save the reframed movie.");
    return result;
}
static void ApplyCostumes(PChunkyFile file, ChunkNumber actor, const MarcusLiveEdits::CostumeEdit &edit)
{
    String path; Filename filename;
    path.SetSzs(const_cast<char *>(edit.file->path.u8string().c_str()));
    Require(filename.FBuildFromPath(&path), "Invalid costume event path.");
    std::unique_ptr<ChunkyFile, Release> source(ChunkyFile::PcflOpen(&filename, fcflNil));
    ChunkNumber number; ChildChunkIdentification child;
    Require(source && source->FCopy(kctgGgae, 0, file, &number) &&
        file->FGetKidChidCtg(kctgActr, actor, 0, kctgGgae, &child), "Cannot copy costume events.");
    file->DeleteChild(kctgActr, actor, kctgGgae, child.cki.cno, 0);
    Require(file->FAdoptChild(kctgActr, actor, kctgGgae, number, 0), "Cannot link costume events.");
}
static void ApplyText(PChunkyFile file, ChunkNumber actorNumber, const MarcusTextEdit &edit)
{
    String path; Filename filename;
    path.SetSzs(const_cast<char *>(edit.file->path.u8string().c_str()));
    Require(filename.FBuildFromPath(&path), "Invalid edited text template path.");
    std::unique_ptr<ChunkyFile, Release> source(ChunkyFile::PcflOpen(&filename, fcflNil));
    ChunkNumber textTemplate;
    Require(source && source->FCopy(kctgTmpl, 0, file, &textTemplate), "Cannot embed the edited text template.");
    ChunkIdentification root;
    const bool hadRoot = file->FGetCkiCtg(kctgMvie, 0, &root);
    if (!hadRoot)
    {
        root.ctg = kctgMvie;
        Require(MovieData::FCreateRoot(file, &root.cno), "Cannot create the edited movie header.");
        ChunkIdentification scene;
        for (LegacyLong i = 0; file->FGetCkiCtg(kctgScen, i, &scene); ++i)
            Require(file->FAdoptChild(kctgMvie, root.cno, kctgScen, scene.cno, i), "Cannot retain the edited movie's scenes.");
    }
    const auto retail = RetailSource(file, root.cno, hadRoot ? 2 : 1);
    DataBlock block; ChildChunkIdentification child;
    if (edit.retailFont)
    {
        std::int32_t fontSource = retail;
        Require(file->FGetKidChidCtg(kctgTmpl, textTemplate, 0, kctgTdt, &child) &&
            file->FFind(kctgTdt, child.cki.cno, &block) && block.FUnpackData() &&
            block.FWriteRgb(&fontSource, sizeof(fontSource), 8), "Cannot write the edited text's font reference.");
    }
    ActorChunkOnFile header;
    Require(file->FFind(kctgActr, actorNumber, &block) && ActorData::FReadHeader(&block, &header), "Cannot read the edited text actor.");
    header.tagTmpl = {0, 0, kctgTmpl, textTemplate};
    Require(ActorData::FWriteHeader(file, actorNumber, &header), "Cannot write the edited text actor.");
    if (file->FGetKidChidCtg(kctgActr, actorNumber, 0, kctgTmpl, &child))
        file->DeleteChild(kctgActr, actorNumber, kctgTmpl, child.cki.cno, 0);
    Require(file->FAdoptChild(kctgActr, actorNumber, kctgTmpl, textTemplate, 0), "Cannot link the edited text template.");
    if (edit.hasMaterial)
    {
        ChunkNumber material;
        Require(source->FCopy(kctgMtrl, 0, file, &material), "Cannot copy the text appearance.");
        if (file->FGetKidChidCtg(kctgActr, actorNumber, 0, kctgMtrl, &child))
            file->DeleteChild(kctgActr, actorNumber, kctgMtrl, child.cki.cno, 0);
        Require(file->FAdoptChild(kctgActr, actorNumber, kctgMtrl, material, 0), "Cannot embed the text appearance.");
        Require(file->FGetKidChidCtg(kctgActr, actorNumber, 0, kctgGgae, &child) &&
            file->FFind(kctgGgae, child.cki.cno, &block), "Cannot read text appearance events.");
        std::unique_ptr<GeneralGroup, Release> events(ActorData::PggReadEvents(&block));
        Require(bool(events), "Cannot decode text appearance events.");
        // Material properties describe the whole text object. Reapply at every
        // entrance and remove earlier whole-word material choices.
        for (LegacyLong i = events->IvMac(); i-- > 0;)
        {
            ActorEvent::Base event; events->GetFixed(i, &event);
            if (event.aet == ActorEvent::aetCost) events->Delete(i);
        }
        for (LegacyLong i = 0; i < events->IvMac(); ++i)
        {
            ActorEvent::Base event; events->GetFixed(i, &event);
            if (event.aet != ActorEvent::aetAdd) continue;
            event.aet = ActorEvent::aetCost;
            ActorEvent::Costume costume = {}; costume.tag = {0, nullptr, kctgMtrl, material};
            Require(events->FInsert(++i, sizeof(costume), &costume, &event), "Cannot write text appearance events.");
        }
        std::unique_ptr<GeneralGroup, Release> wire(ActorData::PggWriteEvents(events.get()));
        ChunkNumber number;
        Require(wire && file->FAdd(wire->CbOnFile(), kctgGgae, &number, &block) && wire->FWrite(&block), "Cannot save text appearance events.");
        file->DeleteChild(kctgActr, actorNumber, kctgGgae, child.cki.cno, 0);
        Require(file->FAdoptChild(kctgActr, actorNumber, kctgGgae, number, 0), "Cannot link text appearance events.");
    }
    std::unique_ptr<RollCallTable, Release> roll(RollCallTable::New());
    Require(bool(roll), "Cannot create the edited movie roll call.");
    ChildChunkIdentification rollChild;
    const bool hadRoll = file->FGetKidChidCtg(kctgMvie, root.cno, 0, kctgGst, &rollChild);
    bool named = false;
    String name;
    const auto ansi = MarcusTextAnsi(edit.value.text.c_str());
    name.SetSzs(const_cast<char *>(ansi.c_str()));
    if (hadRoll)
    {
        int big;
        Require(file->FFind(kctgGst, rollChild.cki.cno, &block), "Cannot read the movie roll call.");
        std::unique_ptr<RollCallTable, Release> wire(RollCallTable::Read(&block, &big));
        Require(wire && wire->CbExtra() == MM_MOVIE_ACTOR_BYTES, "Invalid movie roll call.");
        for (LegacyLong i = 0; i < wire->IvMac(); ++i)
        {
            unsigned char bytes[MM_MOVIE_ACTOR_BYTES]; MMMemoryStream memory; MMMovieActor actor;
            Require(mm_rollcall_table_extra(&wire->storage, i, bytes) && mm_memory_stream_init(&memory, bytes, sizeof(bytes)) &&
                mm_movie_read_actor(&memory.stream, big, &actor, nullptr) == MM_MOVIE_OK, "Cannot decode an existing object name.");
            RollCallActorEntry entry = {};
            entry.arid = actor.actor_id; entry.cactRef = actor.reference_count; entry.grfbrws = actor.browser_flags;
            entry.tagTmpl = {actor.source_id, nullptr, actor.template_type, actor.template_number};
            String existingName; wire->GetStn(i, &existingName);
            if (entry.arid == header.arid)
            { entry.tagTmpl = {0, nullptr, kctgTmpl, textTemplate}; entry.grfbrws = 2; existingName = name; named = true; }
            Require(roll->FAddStn(&existingName, &entry), "Cannot retain an existing object name.");
        }
    }
    if (!named)
    {
        RollCallActorEntry entry = {};
        entry.arid = header.arid; entry.cactRef = 1; entry.grfbrws = 2; entry.tagTmpl = {0, nullptr, kctgTmpl, textTemplate};
        Require(roll->FAddStn(&name, &entry), "Cannot name the edited text.");
    }
    if (hadRoll) file->DeleteChild(kctgMvie, root.cno, kctgGst, rollChild.cki.cno, 0);
    ChunkNumber rollNumber;
    Require(MovieData::FWriteRollCall(file, root.cno, roll.get(), &rollNumber), "Cannot write the edited movie roll call.");
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
        for (const auto &prop : *edits.props)
        {
            if (!prop.file) { ApplyNewProp(file.get(), library.get(), prop); continue; }
            templatePath.SetSzs(const_cast<char *>(prop.file->path.u8string().c_str()));
            Require(templateFile.FBuildFromPath(&templatePath), "Invalid 3D text template path.");
            std::unique_ptr<ChunkyFile, Release> textFile(ChunkyFile::PcflOpen(&templateFile, fcflNil));
            Require(bool(textFile), "Cannot open the 3D text template.");
            ApplyNewProp(file.get(), textFile.get(), prop);
        }
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
        MMI32 *position[3] = {&header.dxyzFullRte.dxr, &header.dxyzFullRte.dyr, &header.dxyzFullRte.dzr};
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
        const auto text = edits.texts->find({scene, actor});
        if (text != edits.texts->end()) ApplyText(file.get(), number, text->second);
        const auto costume = edits.costumes->find({scene, actor});
        if (costume != edits.costumes->end()) ApplyCostumes(file.get(), number, costume->second);
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
