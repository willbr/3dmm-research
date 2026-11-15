#include "scene_edit.h"
#include "mm/content_vfs.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include "soc_core.h"
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
std::shared_ptr<MarcusMovieFile> MarcusAppendScene(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, unsigned long background, int ordinal)
{
    auto result = WorkingCopy(source, std::move(origin));
    String path;
    Filename filename;
    path.SetSzs(const_cast<char *>(result->path.u8string().c_str()));
    Require(filename.FBuildFromPath(&path), "The movie working path is not supported.");
    std::unique_ptr<ChunkyFile, Release> file(source.empty() ? ChunkyFile::PcflCreate(&filename, fcflWriteEnable)
                                                          : ChunkyFile::PcflOpen(&filename, fcflWriteEnable));
    Require(bool(file), "Cannot open the movie working file.");
    ChunkIdentification root;
    const bool hadRoot = file->FGetCkiCtg(kctgMvie, 0, &root);
    if (!hadRoot)
    {
        root.ctg = kctgMvie;
        Require(MovieData::FCreateRoot(file.get(), &root.cno), "Cannot create the movie header.");
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
    DataBlock block;
    short order = kboCur;
    const bool hadSources = file->FGetKidChidCtg(kctgMvie, root.cno, 1, kctgGst, &child);
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
        LegacyLong fallback = hadRoot || source.empty() ? 2 : 1;
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
    if (hadSources) file->DeleteChild(kctgMvie, root.cno, kctgGst, child.cki.cno, 1);
    Require(file->FAdoptChild(kctgMvie, root.cno, kctgGst, sourceChunk, 1), "Cannot link movie content sources.");
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
    TAGOnFile tag = {sourceId, 0, kctgBkgd, static_cast<ChunkNumber>(background)};
    SceneEvent event = {1, sevtSetBkgd};
    Require(starts && frames && starts->FInsert(0, sizeof(tag), &tag, &event) &&
            scene.FCreate(file.get(), &number) && SceneData::FWriteEvents(file.get(), number, frames.get(), starts.get()) &&
            file->FAdoptChild(kctgMvie, root.cno, kctgScen, number, slot) && file->FSave(kctgSoc),
            "Cannot add the new scene to the movie.");
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusSetSceneCamera(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, unsigned long scene, long frame, unsigned long camera)
{
    auto result = WorkingCopy(source, std::move(origin));
    String path;
    Filename filename;
    path.SetSzs(const_cast<char *>(result->path.u8string().c_str()));
    Require(filename.FBuildFromPath(&path), "The movie working path is not supported.");
    std::unique_ptr<ChunkyFile, Release> file(ChunkyFile::PcflOpen(&filename, fcflWriteEnable));
    Require(bool(file), "Cannot open the movie working file.");
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
    Require(metadata.FRead(file.get(), scene), "Cannot read scene metadata.");
    if (frame == metadata.firstFrame && file->FGetKidChidCtg(kctgScen, scene, 0, kctgThumbMbmp, &child))
        file->DeleteChild(kctgScen, scene, kctgThumbMbmp, child.cki.cno, 0);
    Require(file->FSave(kctgSoc), "Cannot save the scene camera working copy.");
    return result;
}
