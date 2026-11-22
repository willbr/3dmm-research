#include "scene_edit.h"
#include <algorithm>
#include <exception>
#include <cstring>
#include <stdexcept>
#include "soc_core.h"
#include "archive_file.h"
#include "private_archive_host.h"
#include "platform_text.h"
#include "mm/actor_archive.h"
#include "mm/scene_data.h"
#include "mm/text.h"
#include "mm/text_resource.h"
#include "mm/scene_reframe.h"
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
    auto result = MarcusCreateWorkingFile(MARCUS_WORKING_MOVIE, source);
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

static void CheckTextName(int status)
{
    static const char *const messages[] = {
        nullptr,
        "Enter some text first.",
        "3D text needs 1 to 50 characters, including at least one visible character.",
        "Enter valid UTF-8 for 3D text.",
        "Use a single line of characters supported by the original 3D fonts."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_TEXT_NAME_CHARACTERS + 1, "3D text diagnostics");
    Require(status == MM_TEXT_NAME_OK, messages[status]);
}

MMTextAuthorName MarcusPrepareTextName(const char *utf8)
{
    MMTextAuthorName name;
    CheckTextName(mm_text_author_prepare_name(utf8, &name));
    return name;
}
std::shared_ptr<MarcusMovieFile> MarcusCreateTextTemplate(MMTextAuthorName textName, MMU32 font, MMI32 source, MMI32 shape)
{
    Require(shape >= 0 && shape < MM_TEXT_SHAPE_COUNT, "Choose a valid 3D text shape.");
    auto result = WorkingCopy({}, nullptr);
    String text;
    text.SetSzs(textName.legacy);
    auto file = MarcusOpenArchive(result->path, MM_PRIVATE_ARCHIVE_CREATE);
    Require(bool(file), "Cannot create the 3D text template.");
    const MMTextDescriptor draft{shape, {source, nullptr, kctgTdf, font}};
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

static void CheckAppearanceEdit(const MarcusArchiveActorResult &edit)
{
    Require(edit.text != MARCUS_ARCHIVE_TEXT_OPEN, "Cannot open the edited text template.");
    CheckActorAuthor(edit.text, edit.text_detail);
    Require(edit.costume != MM_ACTOR_EDIT_ARCHIVE_COPY, "Cannot copy costume events.");
    CheckActorArchiveEdit(edit.costume);
}

struct ArchiveDraftHost
{
    std::exception_ptr failure;

    MarcusArchiveDrafts Services()
    {
        return {this, [](void *p, const void *file) -> MMPrivateArchive * {
            try
            {
                return MarcusOpenArchive(MarcusEditFile::File(file)->path, MM_PRIVATE_ARCHIVE_READ).release();
            }
            catch (...)
            {
                static_cast<ArchiveDraftHost *>(p)->failure = std::current_exception();
                return nullptr;
            }
        }, KauaiText::Services()};
    }
    void Check() const
    {
        if (failure)
            std::rethrow_exception(failure);
    }
};

std::shared_ptr<MarcusMovieFile> MarcusApplyAnimation(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusMovieFile &track,
    const MMActorArchiveAnimation &animation, const MMMovieActorTextEdit *text)
{
    auto result = WorkingCopy(path, origin);
    const auto profile = KauaiPrivateArchiveProfile();
    const auto workingPath = result->path.u8string();
    const auto trackPath = track.path.u8string();
    ArchiveDraftHost host;
    const auto drafts = host.Services();
    MarcusArchiveAnimationResult edit;
    const auto status = marcus_archive_animate(workingPath.c_str(), trackPath.c_str(), &profile, &animation,
                                               text, &drafts, kctgSoc, &edit);
    host.Check();
    Require(status != MARCUS_ARCHIVE_ANIMATION_OPEN, "Cannot open the movie or the new animation track.");
    CheckAppearanceEdit(edit.actor);
    Require(edit.actor.status != MM_ACTOR_EDIT_ARCHIVE_SCENE, "Cannot find the selected actor in the working movie.");
    CheckActorArchiveEdit(edit.actor.status);
    CheckSceneFrameEdit(edit.scene);
    Require(status == MARCUS_ARCHIVE_ANIMATION_OK, "Cannot save the animation edit.");
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusMaterializeEdits(const std::filesystem::path &source,
    std::shared_ptr<MarcusMovieFile> origin, const MMMoviePoseJournal &poses,
    const MMMovieRangeJournal &ranges, const MarcusLiveEdits &edits, const std::filesystem::path &templates)
{
    auto result = WorkingCopy(source, std::move(origin));
    const auto profile = KauaiPrivateArchiveProfile();
    const auto workingPath = result->path.u8string();
    const auto templatePath = edits.props->count ? templates.u8string() : std::string{};
    const MarcusArchiveMovieEdits pending{edits.scenes.get(), edits.storyboards.get(), &ranges, edits.props.get(),
        &poses, edits.texts.get(), edits.costumes.get(), edits.cameras.get()};
    ArchiveDraftHost host;
    const auto drafts = host.Services();
    MarcusArchiveMovieResult edit;
    const int status = marcus_archive_materialize(workingPath.c_str(), source.empty(), templatePath.c_str(), &pending,
                                                  &drafts, &profile, kctgSoc, &edit);
    host.Check();
    switch (status)
    {
    case MARCUS_MATERIALIZE_OK: break;
    case MARCUS_MATERIALIZE_OPEN: throw std::runtime_error("Cannot open the movie working file.");
    case MARCUS_MATERIALIZE_SCENE: CheckSceneArchive(edit.status); break;
    case MARCUS_MATERIALIZE_SOURCE: CheckMovieSource(edit.status); break;
    case MARCUS_MATERIALIZE_BOARD: throw std::runtime_error(edit.board_error);
    case MARCUS_MATERIALIZE_FRAME: CheckSceneFrameEdit(edit.status); break;
    case MARCUS_MATERIALIZE_LIBRARY: throw std::runtime_error("Cannot open the prop library.");
    case MARCUS_MATERIALIZE_TEXT_OPEN: throw std::runtime_error("Cannot open the 3D text template.");
    case MARCUS_MATERIALIZE_TEXT_NAME: CheckTextName(edit.status); break;
    case MARCUS_MATERIALIZE_INSERT: CheckActorAuthor(edit.status, edit.detail); break;
    case MARCUS_MATERIALIZE_MEMORY: throw std::bad_alloc();
    case MARCUS_MATERIALIZE_ACTOR:
        CheckAppearanceEdit(edit.actor);
        CheckActorArchiveEdit(edit.actor.status);
        break;
    case MARCUS_MATERIALIZE_ROLLCALL: throw std::runtime_error("Cannot write the movie's empty roll call.");
    case MARCUS_MATERIALIZE_SAVE: throw std::runtime_error("Cannot save the object working copy.");
    default: throw std::runtime_error("Unexpected movie materialization status.");
    }
    return result;
}
