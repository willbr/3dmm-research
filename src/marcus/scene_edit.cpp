#include "scene_edit.h"
#include <algorithm>
#include <exception>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include "soc_core.h"
#include "archive_file.h"
#include "platform_text.h"
#include "mm/actor_archive.h"
#include "mm/scene_data.h"
#include "mm/text.h"
#include "mm/text_resource.h"
#include "mm/scene_reframe.h"
#include "storyboard_native.h"
#undef size
ASSERTNAME

namespace
{
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
static void CheckMovieSource(int status)
{
    static const char *const messages[] = {
        nullptr,
        "Cannot read movie content sources.",
        "The movie content source table is invalid.",
        "This movie has no available content source IDs.",
        "Cannot allocate the movie content sources.",
        "Cannot write movie content sources.",
        "Cannot link movie content sources.",
        "The copied object uses an unknown content source."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_MOVIE_SOURCE_EDIT_UNKNOWN + 1, "source diagnostics");
    Require(status == MM_MOVIE_SOURCE_EDIT_OK, messages[status]);
}

static LegacyLong RetailSource(const MMMovieArchiveEditor &editor, ChunkNumber movie, LegacyLong fallbackSource)
{
    MMI32 id;
    CheckMovieSource(mm_movie_archive_register_source(&editor, KauaiText::Services(), movie, fallbackSource, nullptr, &id));
    return id;
}

static void CheckSceneArchive(int status)
{
    static const char *const messages[] = {
        nullptr,
        "Cannot create the movie header.",
        "Cannot retain the movie's scene order.",
        "This movie has no available scene slots.",
        "Cannot create the new scene.",
        "Cannot add the new scene to the movie."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_MOVIE_SCENE_ARCHIVE_LINK + 1, "scene diagnostics");
    Require(status == MM_MOVIE_SCENE_ARCHIVE_OK, messages[status]);
}

static MMMovieArchiveRoot EnsureMovieRoot(const MMMovieArchiveEditor &editor)
{
    MMMovieArchiveRoot root;
    CheckSceneArchive(mm_movie_archive_ensure_root(&editor, &root));
    return root;
}

static void ApplyNewScene(const MMMovieArchiveEditor &editor, const MMPreviewLiveScene &definition, bool emptyMovie)
{
    const auto root = EnsureMovieRoot(editor);
    const auto source = RetailSource(editor, root.number, root.created && !emptyMovie ? 1 : 2);
    CheckSceneArchive(mm_movie_archive_append_scene(&editor, root.number, definition.number, source,
                                                    definition.background));
}
static void CheckSceneFrameEdit(int status)
{
    static const char *const messages[] = {
        nullptr,
        "Cannot read scene metadata.",
        "The scene range must retain its original frames.",
        "Cannot read scene frame events.",
        "Cannot decode scene frame events.",
        "Cannot insert the scene camera change.",
        "Cannot write the edited scene frame events.",
        "Cannot link the edited scene frame events.",
        "Cannot write the extended scene."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_SCENE_ARCHIVE_EDIT_WRITE_SCENE + 1,
                  "scene archive edit diagnostics");
    Require(status == MM_SCENE_ARCHIVE_EDIT_OK, messages[status]);
}

static void ApplyCamera(const MMMovieArchiveEditor &editor, unsigned long number, long key, unsigned long camera)
{
    CheckSceneFrameEdit(mm_scene_archive_set_camera(&editor, static_cast<MMU32>(number),
                                                  static_cast<MMI32>(key), static_cast<MMU32>(camera)));
}

static void ApplySceneRange(const MMMovieArchiveEditor &editor, const MarcusSceneRange &range)
{
    CheckSceneFrameEdit(mm_scene_archive_set_range(&editor, static_cast<MMU32>(range.scene), range.first, range.last));
}
static void CheckActorArchiveEdit(int result)
{
    static const char *const messages[] = {
        nullptr,
        "The selected object does not belong to this scene.",
        "Cannot read the object placement.",
        "The object placement or shape is out of range.",
        "Cannot write the object placement.",
        "Cannot retain the object animation.",
        "Cannot save the object's appearance.",
        "Cannot read the object's animation events.",
        "Cannot edit the object's animation events.",
        "Cannot write the object's animation events.",
        "Cannot link the object's animation events.",
        "Cannot link the moved object."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_ACTOR_EDIT_ARCHIVE_LINK_ACTOR + 1, "actor edit diagnostics");
    Require(result == MM_ACTOR_EDIT_ARCHIVE_OK, messages[result]);
}

static void CheckActorRegistration(int status)
{
    static const char *const messages[] = {
        nullptr,
        "No available child slots for the pasted object.",
        "Cannot read the scene's object list.",
        "The scene start events are invalid.",
        "Cannot register the scene object.",
        "Cannot link scene start events.",
        "Cannot read the movie roll call.",
        "The movie roll call is invalid.",
        "Cannot write the movie roll call.",
        "Cannot allocate the scene object listing."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_ACTOR_REGISTER_MEMORY + 1, "registration diagnostics");
    Require(status == MM_ACTOR_REGISTER_OK, messages[status]);
}

static void CheckActorAuthor(int status, int detail)
{
    static const char *const messages[] = {
        nullptr,
        "Cannot read the selected prop template.",
        "The selected object template is invalid.",
        "The prop insertion frame is outside this scene.",
        "The object position is outside the native coordinate range.",
        nullptr,
        nullptr,
        "Cannot embed the 3D text template.",
        "Cannot write the 3D text font reference.",
        "Cannot read the edited text actor.",
        "Cannot write the actor instance.",
        "The new object ID is already in use.",
        "Cannot link the embedded 3D text template.",
        "Cannot write the object's stationary track.",
        "Cannot copy the text appearance.",
        "Cannot embed the text appearance.",
        nullptr,
        nullptr
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_ACTOR_AUTHOR_REGISTER + 1,
                  "actor archive diagnostics");
    switch (status)
    {
    case MM_ACTOR_AUTHOR_ROOT: CheckSceneArchive(detail); break;
    case MM_ACTOR_AUTHOR_SOURCE: CheckMovieSource(detail); break;
    case MM_ACTOR_AUTHOR_MATERIAL_EVENTS: CheckActorArchiveEdit(detail); break;
    case MM_ACTOR_AUTHOR_REGISTER: CheckActorRegistration(detail); break;
    default: Require(status == MM_ACTOR_AUTHOR_OK, messages[status]); break;
    }
}

namespace
{
struct ActorArchiveFiles
{
    MMPrivateArchive *source, *destination;

    static int Copy(void *value, MMU32 type, MMU32 number, MMU32 *copied)
    {
        const auto &io = *static_cast<ActorArchiveFiles *>(value);
        return mm_private_archive_copy(io.source, io.destination, type, number, 0, copied);
    }

    static int Clone(void *value, MMU32 actor, MMU32 *copied)
    {
        const auto &io = *static_cast<ActorArchiveFiles *>(value);
        return mm_private_archive_copy(io.source, io.destination, kctgActr, actor, 1, copied);
    }
};
}

static void ApplyNewProp(MMPrivateArchive *file, MMPrivateArchive *library, const MMMovieArchiveEditor &destination,
                         const MarcusNamedProp &prop)
{
    const auto source = mm_private_archive_editor(library);
    ActorArchiveFiles files{library, file};
    const MMActorAuthorHost host{&source, &destination, KauaiText::Services(), &files, ActorArchiveFiles::Copy};
    const auto name = prop.file ? MarcusTextAnsi(prop.name.c_str()) : prop.name;
    String caption;
    caption.SetSzs(const_cast<char *>(name.c_str()));
    const auto &definition = prop.definition;
    const MMActorInsert insert{definition.scene, definition.number, definition.templateNumber,
        definition.actorId, definition.first, definition.last, definition.point, caption.Prgch(),
        static_cast<MMU32>(caption.Cch()), bool(prop.file)};
    int detail = 0;
    const int status = mm_actor_archive_insert(&host, &insert, &detail);
    CheckActorAuthor(status, detail);
}

std::string MarcusTextAnsi(const char *utf8)
{
    static const char *const messages[] = {
        nullptr,
        "Enter some text first.",
        "3D text needs 1 to 50 characters, including at least one visible character.",
        "Enter valid UTF-8 for 3D text.",
        "Use a single line of characters supported by the original 3D fonts."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_TEXT_NAME_CHARACTERS + 1, "3D text diagnostics");
    char name[MM_TEXT_NAME_BYTES];
    const auto status = mm_text_author_name(utf8, name);
    Require(status == MM_TEXT_NAME_OK, messages[status]);
    return name;
}
std::shared_ptr<MarcusMovieFile> MarcusCreateTextTemplate(const char *utf8, unsigned long font, long source, int shape)
{
    const auto ansi = MarcusTextAnsi(utf8);
    Require(shape >= 0 && shape < tdtsLim, "Choose a valid 3D text shape.");
    auto result = WorkingCopy({}, nullptr);
    String text;
    text.SetSzs(const_cast<char *>(ansi.c_str()));
    auto file = MarcusOpenArchive(result->path, MM_PRIVATE_ARCHIVE_CREATE);
    Require(bool(file), "Cannot create the 3D text template.");
    const MMTextDescriptor draft{shape, {static_cast<MMI32>(source), nullptr, kctgTdf, static_cast<MMU32>(font)}};
    const auto writer = mm_private_archive_writer(file.get());
    byte wire[kcbMaxDataStn];
    const auto name = MarcusArchiveName(text, wire);
    ChunkNumber number;
    Require(mm_text_write_draft(&name, &draft, &number, &writer) && number == 0 &&
                mm_private_archive_save(file.get(), kctgSoc),
        "Cannot create the 3D text template.");
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusCreateEventFile() { return WorkingCopy({}, nullptr); }
std::shared_ptr<MarcusMovieFile> MarcusCopyMovie(const std::shared_ptr<MarcusMovieFile> &source)
{ return WorkingCopy(source->path, source); }
std::shared_ptr<MarcusMovieFile> MarcusCopyMovie(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &source)
{ return WorkingCopy(path, source); }

namespace
{
ChunkNumber TransferObject(ActorArchiveFiles files, const MMMovieArchiveEditor &source, const MMMovieArchiveEditor &destination,
                           const MMActorArchiveRequest &request, MMMovieArchiveSourceMap *sources,
                           MMActorHeader *header)
{
    static_assert(knfrmInvalid == 2147483647, "actor invalid frame");
    const MMActorArchiveHost host{&files, &source, &destination, ActorArchiveFiles::Clone, ActorArchiveFiles::Copy,
                                  sources, sources ? mm_movie_archive_map_source : nullptr};

    static const char *const messages[] = {
        nullptr,
        "Cannot copy the object's native track.",
        "The pasted object track would exceed the frame range.",
        "The embedded template is missing.",
        "Cannot read the copied 3D text font.",
        "Invalid copied 3D text font.",
        "Cannot write the copied 3D text font.",
        "The copied object is missing an embedded asset.",
        "No available child slots for the pasted object.",
        "Cannot retain the object's embedded asset.",
        "Cannot link the copied actor sound.",
        "The copied object uses an unknown content source.",
        "Cannot write the pasted object ID.",
        "Cannot read the object's animation events.",
        "Cannot write the pasted animation events.",
        "Cannot allocate the copied sound links."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_ACTOR_ARCHIVE_MEMORY + 1, "archive diagnostics");
    ChunkNumber copied;
    const int status = mm_actor_archive_transfer(&request, &host, &copied, header);
    if (sources) CheckMovieSource(sources->result);
    Require(status == MM_ACTOR_ARCHIVE_OK, messages[status]);
    return copied;
}
}

std::unique_ptr<MarcusObjectClipboard> MarcusCopyObject(const std::filesystem::path &movie, unsigned long scene,
    long frame, const MarcusObject &object)
{
    MM::Archive archive;
    MM::MovieDirectory catalogue;
    MM::Error error;
    if (!archive.OpenForInspection(movie.u8string(), error) || !archive.ReadMovieDirectory(catalogue, error))
        throw std::runtime_error(error.message);
    auto result = std::make_unique<MarcusObjectClipboard>();
    result->sources = std::move(catalogue.sources);
    if (!catalogue.hasRoot) result->sources.push_back({1, "3D Movie Maker/3DMOVIE"});
    result->frame = frame;
    unsigned short name[sizeof(object.name)];
    std::size_t units;
    Require(mm_text_read_utf8(object.name, std::strlen(object.name), name, sizeof(name) / sizeof(name[0]), &units) == 1,
            "Cannot decode the object name.");
    char ansi[sizeof(object.name)];
    const auto bytes = mm_text_ansi_from_units(nullptr, name, units, ansi, sizeof(ansi), 0);
    Require(!units || bytes, "Cannot encode the object name.");
    result->name.assign(ansi, bytes);
    result->browserFlags = object.text ? 2 : object.prop ? 1 : 0;
    result->file = MarcusCreateEventFile();
    auto source = MarcusOpenArchive(movie, MM_PRIVATE_ARCHIVE_READ);
    auto destination = MarcusOpenArchive(result->file->path, MM_PRIVATE_ARCHIVE_CREATE);
    Require(source && destination, "Cannot create the object clipboard.");
    const auto sourceEditor = mm_private_archive_editor(source.get());
    const auto destinationEditor = mm_private_archive_editor(destination.get());
    Require(destinationEditor.add(destinationEditor.context, kctgScen, 0, &result->sounds, nullptr),
            "Cannot create the object clipboard.");
    const auto sceneNumber = static_cast<MMU32>(scene);
    ChunkNumber actor;
    Require(mm_actor_archive_find(&sourceEditor, sceneNumber, object.actor_id, &actor),
            "Cannot find the selected object in the movie.");
    const MMActorArchiveRequest request{actor, sceneNumber, result->sounds, object.actor_id, 0, 0};
    result->actor = TransferObject({source.get(), destination.get()}, sourceEditor, destinationEditor, request, nullptr, nullptr);
    Require(mm_private_archive_save(destination.get(), kctgSoc), "Cannot save the object clipboard.");
    return result;
}

std::shared_ptr<MarcusMovieFile> MarcusPasteObject(const std::filesystem::path &movie,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusObjectClipboard &clipboard,
    unsigned long sceneNumber, long frame, std::int32_t actorId)
{
    auto result = WorkingCopy(movie, origin);
    auto source = MarcusOpenArchive(clipboard.file->path, MM_PRIVATE_ARCHIVE_READ);
    auto destination = MarcusOpenArchive(result->path, MM_PRIVATE_ARCHIVE_EDIT);
    Require(source && destination, "Cannot open the movie or object working copy.");
    const auto sourceEditor = mm_private_archive_editor(source.get());
    const auto destinationEditor = mm_private_archive_editor(destination.get());
    const auto targetScene = static_cast<MMU32>(sceneNumber);
    MMSceneState scene;
    Require(mm_scene_archive_read_header(&destinationEditor, targetScene, &scene) &&
                frame >= scene.firstFrame && frame <= scene.lastFrame,
            "The paste frame is outside the scene.");
    const auto root = EnsureMovieRoot(destinationEditor);
    // Headless scene-only movies rely on an implicit retail source.
    if (root.created) RetailSource(destinationEditor, root.number, 1);
    std::vector<MMMovieArchiveSource> sources;
    sources.reserve(clipboard.sources.size());
    for (const auto &sourceEntry : clipboard.sources)
        sources.push_back({sourceEntry.id, sourceEntry.name.c_str(), 0, 0});
    MMMovieArchiveSourceMap sourceMap{&destinationEditor, KauaiText::Services(), sources.data(), sources.size(),
                                     root.number, root.created ? 1 : 2, MM_MOVIE_SOURCE_EDIT_OK};
    mm_movie_archive_prepare_source_map(&sourceMap);

    const MMActorArchiveRequest request{clipboard.actor, clipboard.sounds, targetScene, actorId,
                                        static_cast<MMI32>(clipboard.frame), static_cast<MMI32>(frame)};
    MMActorHeader header;
    const auto actor = TransferObject({source.get(), destination.get()}, sourceEditor, destinationEditor, request, &sourceMap, &header);
    const MMActorArchivePlacement placement{targetScene, actor, scene.firstFrame, true};
    String caption;
    caption.SetSzs(const_cast<char *>(clipboard.name.c_str()));
    const MMActorArchiveName entry{
        {header.actor_id, 1, header.source_id, static_cast<MMU32>(clipboard.browserFlags),
         header.template_type, header.template_number},
        caption.Prgch(), static_cast<MMU32>(caption.Cch()), 0};
    CheckActorRegistration(mm_actor_archive_register(&destinationEditor, root.number, &placement, &entry));
    Require(mm_private_archive_save(destination.get(), kctgSoc), "Cannot save the pasted object.");
    return result;
}

std::shared_ptr<MarcusMovieFile> MarcusReframeScene(const std::filesystem::path &movie,
    const std::shared_ptr<MarcusMovieFile> &origin, unsigned long scene, long frame,
    const MarcusProjection &work, const MarcusProjection &camera,
    const std::map<std::int32_t, MM::PreviewTrackPose> &poses)
{
    auto result = WorkingCopy(movie, origin);
    auto file = MarcusOpenArchive(result->path, MM_PRIVATE_ARCHIVE_EDIT);
    Require(bool(file), "Cannot open the camera transform working copy.");

    const auto editor = mm_private_archive_editor(file.get());
    const MMSceneReframeCamera workCamera{work.transform, work.vertical_fov, work.aspect};
    const MMSceneReframeCamera fixedCamera{camera.transform, camera.vertical_fov, camera.aspect};
    static_assert(sizeof(BMAT34) == sizeof(MMActorMatrix), "reframe matrix layout");
    const MMSceneReframeHost host{
        const_cast<std::map<std::int32_t, MM::PreviewTrackPose> *>(&poses),
        [](void *context, MMI32 id, MMSceneReframePose *out) -> int {
            const auto &tracks = *static_cast<const std::map<std::int32_t, MM::PreviewTrackPose> *>(context);
            const auto found = tracks.find(id);
            if (found == tracks.end()) return false;
            const auto &pose = found->second;
            out->location = {pose.node, pose.offset, pose.routeFrame};
            out->position = {pose.position[0], pose.position[1], pose.position[2]};
            out->translation = {pose.translation[0], pose.translation[1], pose.translation[2]};
            std::copy(std::begin(pose.rotation), std::end(pose.rotation), out->rotation);
            return true;
        },
        [](const MMActorEntrance *entry, MMActorMatrix *matrix) {
            br_euler angles = {entry->x, entry->y, entry->z, BR_EULER_XYZ_S};
            BMAT34 native;
            BrEulerToMatrix34(&native, &angles);
            std::memcpy(matrix, &native, sizeof(native));
        },
        [](const MMActorMatrix *matrix, MMActorEntrance *entry) {
            BMAT34 native;
            std::memcpy(&native, matrix, sizeof(native));
            br_euler angles = {entry->x, entry->y, entry->z, BR_EULER_XYZ_S};
            BrMatrix34ToEuler(&angles, &native);
            entry->x = angles.a;
            entry->y = angles.b;
            entry->z = angles.c;
        }
    };
    static const char *const messages[] = {
        nullptr,
        "The work and movie cameras need valid perspective lenses.",
        "The camera transform would move an object out of range.",
        "This scene has no objects to transform.",
        "Cannot allocate the reframed object tracks.",
        "Cannot read an object for the camera transform.",
        "Cannot copy an object and retain its content.",
        "Cannot read an object track for the camera transform.",
        "Invalid object entrance for the camera transform.",
        "Invalid object position on its track.",
        "Invalid object subroute.",
        "Cannot insert the reframed animation events.",
        "Cannot write the reframed object track.",
        "Cannot link the reframed object."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_SCENE_REFRAME_LINK + 1, "reframe diagnostics");
    const auto status = mm_scene_archive_reframe(&editor, static_cast<MMU32>(scene), static_cast<MMI32>(frame),
                                                 &workCamera, &fixedCamera, &host);
    Require(status == MM_SCENE_REFRAME_OK, messages[status]);
    Require(mm_private_archive_save(file.get(), kctgSoc), "Cannot save the reframed movie.");
    return result;
}

static void ApplyText(MMPrivateArchive *file, ChunkNumber actor, const MarcusTextEdit &edit);
static void ApplyCostumes(MMPrivateArchive *file, ChunkNumber actor, const MarcusLiveEdits::CostumeEdit &edit)
{
    auto source = MarcusOpenArchive(edit.file->path, MM_PRIVATE_ARCHIVE_READ);
    ChunkNumber number;
    Require(source && mm_private_archive_copy(source.get(), file, kctgGgae, 0, 0, &number),
            "Cannot copy costume events.");
    const auto editor = mm_private_archive_editor(file);
    CheckActorArchiveEdit(mm_actor_archive_link_events(&editor, actor, number));
}
std::shared_ptr<MarcusMovieFile> MarcusApplyAnimation(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusMovieFile &track,
    unsigned long scene, long actorId, long previousActorFrame, int lastActorFrame, int lastEditedFrame,
    const MM::PreviewObjectShape &shape, const MarcusTextEdit *text)
{
    auto result = WorkingCopy(path, origin);
    auto file = MarcusOpenArchive(result->path, MM_PRIVATE_ARCHIVE_EDIT);
    auto source = MarcusOpenArchive(track.path, MM_PRIVATE_ARCHIVE_READ);
    Require(file && source, "Cannot open the movie or the new animation track.");
    const auto editor = mm_private_archive_editor(file.get());
    struct Track
    {
        MMPrivateArchive *file, *source;
        const MarcusTextEdit *text;
        std::exception_ptr failure;
    } trackEdit{file.get(), source.get(), text, {}};
    const MMActorArchiveAnimation edit{static_cast<MMU32>(scene), static_cast<MMI32>(actorId),
        static_cast<MMI32>(previousActorFrame), lastActorFrame, lastEditedFrame,
        shape.rotation, shape.scale, &trackEdit,
        [](void *p, MMU32 type, MMU32 number, MMU32 *copied) -> int {
            auto &track = *static_cast<Track *>(p);
            try { return mm_private_archive_copy(track.source, track.file, type, number, 0, copied); }
            catch (...) { track.failure = std::current_exception(); return false; }
        },
        [](void *p, MMU32 actor) -> int {
            auto &track = *static_cast<Track *>(p);
            try
            {
                if (track.text) ApplyText(track.file, actor, *track.text);
                return true;
            }
            catch (...) { track.failure = std::current_exception(); return false; }
        }};
    int sceneResult;
    const int resultCode = mm_actor_archive_animate(&editor, &edit, &sceneResult);
    if (trackEdit.failure) std::rethrow_exception(trackEdit.failure);
    Require(resultCode != MM_ACTOR_EDIT_ARCHIVE_SCENE, "Cannot find the selected actor in the working movie.");
    CheckActorArchiveEdit(resultCode);
    CheckSceneFrameEdit(sceneResult);
    Require(mm_private_archive_save(file.get(), kctgSoc), "Cannot save the animation edit.");
    return result;
}
static void ApplyText(MMPrivateArchive *file, ChunkNumber actorNumber, const MarcusTextEdit &edit)
{
    auto source = MarcusOpenArchive(edit.file->path, MM_PRIVATE_ARCHIVE_READ);
    Require(bool(source), "Cannot open the edited text template.");
    const auto destinationEditor = mm_private_archive_editor(file);
    ActorArchiveFiles files{source.get(), file};
    const MMActorAuthorHost host{nullptr, &destinationEditor, KauaiText::Services(), &files, ActorArchiveFiles::Copy};
    const auto name = MarcusTextAnsi(edit.value.text.c_str());
    String caption;
    caption.SetSzs(const_cast<char *>(name.c_str()));
    const MMActorTextReplacement request{actorNumber, caption.Prgch(), static_cast<MMU32>(caption.Cch()),
                                         edit.retailFont, edit.hasMaterial};
    int detail = 0;
    const int status = mm_actor_archive_replace_text(&host, &request, &detail);
    CheckActorAuthor(status, detail);
}
std::shared_ptr<MarcusMovieFile> MarcusMaterializeEdits(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, const std::vector<MarcusPropPlacement> &placements,
    const std::vector<MarcusSceneRange> &ranges, const MarcusLiveEdits &edits, const std::filesystem::path &templates)
{
    auto result = WorkingCopy(source, std::move(origin));
    auto file = MarcusOpenArchive(result->path, source.empty() ? MM_PRIVATE_ARCHIVE_CREATE : MM_PRIVATE_ARCHIVE_EDIT);
    Require(bool(file), "Cannot open the movie working file.");
    const auto editor = mm_private_archive_editor(file.get());
    for (const auto &scene : *edits.scenes) ApplyNewScene(editor, scene, source.empty());
    for (const auto &entry : *edits.storyboards) MarcusEmbedBoard(file.get(), entry.first, entry.second);
    for (const auto &range : ranges) ApplySceneRange(editor, range);
    if (!edits.props->empty())
    {
        auto library = MarcusOpenArchive(templates, MM_PRIVATE_ARCHIVE_READ);
        Require(bool(library), "Cannot open the prop library.");
        for (const auto &prop : *edits.props)
        {
            if (!prop.file) { ApplyNewProp(file.get(), library.get(), editor, prop); continue; }
            auto textFile = MarcusOpenArchive(prop.file->path, MM_PRIVATE_ARCHIVE_READ);
            Require(bool(textFile), "Cannot open the 3D text template.");
            ApplyNewProp(file.get(), textFile.get(), editor, prop);
        }
    }
    for (const auto &placement : placements)
    {
        const auto key = std::make_pair(placement.scene, placement.actor);
        const auto text = edits.texts->find(key);
        const auto costume = edits.costumes->find(key);
        struct Appearance
        {
            MMPrivateArchive *file;
            const MarcusTextEdit *text;
            const MarcusLiveEdits::CostumeEdit *costume;
            std::exception_ptr failure;
        } appearance{file.get(), text == edits.texts->end() ? nullptr : &text->second,
                     costume == edits.costumes->end() ? nullptr : &costume->second, {}};
        const MMActorArchivePose pose{
            static_cast<MMU32>(placement.scene), static_cast<MMU32>(placement.actor), placement.delta,
            placement.shape.rotation, placement.shape.scale, &appearance,
            [](void *value, MMU32 copied) -> int {
                auto &edit = *static_cast<Appearance *>(value);
                try
                {
                    if (edit.text) ApplyText(edit.file, copied, *edit.text);
                    if (edit.costume) ApplyCostumes(edit.file, copied, *edit.costume);
                    return true;
                }
                catch (...)
                {
                    edit.failure = std::current_exception();
                    return false;
                }
            }
        };
        const auto result = mm_actor_archive_place(&editor, &pose);
        if (appearance.failure) std::rethrow_exception(appearance.failure);
        CheckActorArchiveEdit(result);
    }
    for (const auto &entry : *edits.cameras)
        ApplyCamera(editor, entry.scene, entry.frame, entry.camera);
    Require(mm_movie_archive_ensure_empty_rollcall(&editor), "Cannot write the movie's empty roll call.");
    Require(mm_private_archive_save(file.get(), kctgSoc), "Cannot save the object working copy.");
    return result;
}
