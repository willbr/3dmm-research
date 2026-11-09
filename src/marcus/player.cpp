#include "player.h"
#include "three_mm/preview.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <exception>

namespace
{
const std::uint32_t sceneType = 0x5343454e, movieType = 0x4d564945, gstType = 0x47535420;
template <std::size_t N> void Copy(char (&output)[N], const char *input)
{
    const auto count = std::min(N - 1, std::strlen(input));
    std::memcpy(output, input, count);
    output[count] = 0;
}
template <std::size_t N> void Copy(char (&output)[N], const std::string &input)
{
    // Do not cut a UTF-8 sequence in half at the fixed C record boundary.
    auto count = std::min(N - 1, input.size());
    while (count && count < input.size() && (static_cast<unsigned char>(input[count]) & 0xc0) == 0x80)
        --count;
    std::memcpy(output, input.data(), count);
    output[count] = 0;
}
std::int32_t Word(const std::vector<std::uint8_t> &bytes, bool big)
{
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= std::uint32_t(bytes[i]) << ((big ? 3 - i : i) * 8);
    std::int32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
} // namespace
struct MarcusPlayer
{
    ThreeMM::Preview preview;
    ThreeMM::PreviewImage image;
    std::vector<ThreeMM::Id> scenes;
    std::vector<MarcusScene> sceneInfo;
    std::vector<ThreeMM::PreviewObject> objects;
    std::map<std::int32_t, std::string> names;
    char error[1024] = {};
    int selected = -1;
};
namespace
{
bool Render(MarcusPlayer *player, const ThreeMM::PreviewView &view, MarcusImage *image, ThreeMM::Error &error)
{
    if (!image || player->selected < 0 || !player->preview.Render(view, player->image, error))
        return false;
    image->width = static_cast<int>(player->image.width);
    image->height = static_cast<int>(player->image.height);
    image->pitch = image->width * 4;
    image->rgba = player->image.rgba.data();
    const auto &camera = player->image.camera;
    image->camera = {camera.x, camera.y, camera.z, camera.yaw, camera.pitch, camera.moveSpeed};
    return true;
}
template <typename F> int Guard(MarcusPlayer *player, F operation)
{
    if (!player)
        return 0;
    player->error[0] = 0;
    try
    {
        ThreeMM::Error error;
        if (operation(error))
            return 1;
        Copy(player->error, error.message.empty() ? "The player could not complete this operation." : error.message);
    }
    catch (const std::exception &error)
    {
        Copy(player->error, error.what());
    }
    catch (...)
    {
        Copy(player->error, "Unexpected error in the movie backend.");
    }
    return 0;
}
std::size_t Find(const ThreeMM::Archive &archive, ThreeMM::Id id)
{
    const auto &chunks = archive.Chunks();
    for (std::size_t i = 0; i < chunks.size(); ++i)
        if (chunks[i].id == id)
            return i;
    return chunks.size();
}
bool Strings(ThreeMM::Archive &archive, ThreeMM::Id id, std::vector<ThreeMM::StringTableEntry> &entries, bool &big,
             ThreeMM::Error &error)
{
    const auto index = Find(archive, id);
    ThreeMM::PayloadPage header;
    if (index == archive.Chunks().size() || !archive.ReadPayload(index, 0, 2, header, error) ||
        header.bytes.size() != 2)
        return false;
    big = header.bytes[0] == 0;
    ThreeMM::StringTablePage page;
    for (std::uint32_t first = 0;; first += static_cast<std::uint32_t>(page.entries.size()))
    {
        if (!archive.ReadStrings(index, first, 128, page, error))
            return false;
        entries.insert(entries.end(), page.entries.begin(), page.entries.end());
        if (first + page.entries.size() >= page.totalEntries)
            return true;
        if (entries.size() > 65536 || page.entries.empty())
            return false;
    }
}
} // namespace
extern "C"
{
    MarcusPlayer *marcus_create(void)
    {
        try
        {
            return new MarcusPlayer;
        }
        catch (...)
        {
            return nullptr;
        }
    }
    void marcus_destroy(MarcusPlayer *player)
    {
        delete player;
    }
    const char *marcus_error(const MarcusPlayer *player)
    {
        return player ? player->error : "Player is unavailable.";
    }
    int marcus_open(MarcusPlayer *player, const char *movie, const char *resources, const char *directory)
    {
        return Guard(player, [&](ThreeMM::Error &error) {
            if (!movie || !resources || !directory)
                return false;
            // Validate the graph and ordered scene list before replacing the live renderer.
            ThreeMM::Archive archive;
            if (!archive.Open(movie, error))
                return false;
            std::vector<ThreeMM::Id> scenes;
            std::vector<MarcusScene> sceneInfo;
            std::map<std::int32_t, std::string> names;
            std::vector<std::int32_t> sources;
            bool hasSourceTable = false;
            const ThreeMM::Chunk *root = nullptr;
            for (const auto &chunk : archive.Chunks())
                if (chunk.id.type == movieType)
                {
                    root = &chunk;
                    break;
                }
            if (root)
            {
                for (const auto &child : root->children)
                {
                    if (child.target.type == sceneType)
                    {
                        if (!child.present)
                        {
                            error.message = "The movie references a missing scene.";
                            return false;
                        }
                        scenes.push_back(child.target);
                    }
                    if (child.target.type != gstType || child.slot > 1)
                        continue;
                    hasSourceTable = hasSourceTable || child.slot == 1;
                    std::vector<ThreeMM::StringTableEntry> entries;
                    bool big = false;
                    if (!Strings(archive, child.target, entries, big, error))
                        return false;
                    for (const auto &entry : entries)
                        if (child.slot == 0 && entry.extra.size() == 28)
                            names[Word(entry.extra, big)] = entry.text;
                        else if (child.slot == 1 && entry.extra.size() == 4 &&
                                 (entry.text.find("3D Movie Maker") != std::string::npos ||
                                  entry.text.find("3DMOVIE") != std::string::npos))
                            sources.push_back(Word(entry.extra, big));
                }
                // Source 2 is the original retail content when older movies omit a table.
                if (!hasSourceTable)
                    sources.push_back(2);
            }
            else
            {
                // Also accept headless scene fixtures, which use source 1.
                for (const auto &chunk : archive.Chunks())
                    if (chunk.id.type == sceneType)
                        scenes.push_back(chunk.id);
                sources.push_back(1);
            }
            if (scenes.empty())
            {
                error.message = "This archive contains no movie scenes. Open a .3MM movie.";
                return false;
            }
            for (const auto &id : scenes)
            {
                const auto index = Find(archive, id);
                ThreeMM::RecordHeader record;
                if (index == archive.Chunks().size() || !archive.ReadRecord(index, record, error) ||
                    record.kind != ThreeMM::RecordKind::scene || record.scene.lastFrame < record.scene.firstFrame)
                    return false;
                MarcusScene scene = {};
                scene.chunk = id.number;
                scene.first_frame = record.scene.firstFrame;
                scene.last_frame = record.scene.lastFrame;
                const auto &name = archive.Chunks()[index].name;
                Copy(scene.name, name.empty() ? "Scene " + std::to_string(sceneInfo.size() + 1) : name);
                sceneInfo.push_back(scene);
            }
            if (!player->preview.Open(movie, resources, error))
                return false;
            player->selected = -1;
            player->scenes.clear();
            player->sceneInfo.clear();
            player->objects.clear();
            player->names.clear();
            player->image = {};
            const char *files[] = {"tmpls.3cn", "mtrls.3cn", "bkgds.3cn", "tdfs.3cn"};
            for (const auto source : sources)
                for (const auto file : files)
                {
                    const auto path = std::string(directory) + "/" + file;
                    if (std::ifstream(path, std::ios::binary) && !player->preview.AddContent(source, path, error))
                        return false;
                }
            player->scenes.swap(scenes);
            player->sceneInfo.swap(sceneInfo);
            player->names.swap(names);
            if (!player->preview.Select(player->scenes[0], error))
                return false;
            player->objects = player->preview.Objects();
            player->selected = 0;
            return true;
        });
    }
    int marcus_scene(const MarcusPlayer *player, int index, MarcusScene *scene)
    {
        if (!player || !scene || index < 0 || static_cast<std::size_t>(index) >= player->sceneInfo.size())
            return 0;
        *scene = player->sceneInfo[index];
        return 1;
    }
    int marcus_select_scene(MarcusPlayer *player, int index)
    {
        return Guard(player, [&](ThreeMM::Error &error) {
            if (index < 0 || static_cast<std::size_t>(index) >= player->scenes.size())
                return false;
            player->selected = -1;
            player->objects.clear();
            if (!player->preview.Select(player->scenes[index], error))
                return false;
            player->objects = player->preview.Objects();
            player->selected = index;
            return true;
        });
    }
    int marcus_info(const MarcusPlayer *player, MarcusInfo *info)
    {
        if (!player || !info || player->selected < 0)
            return 0;
        const auto &source = player->preview.Info();
        info->scene = player->selected;
        info->scene_count = static_cast<int>(player->scenes.size());
        info->object_count = static_cast<int>(player->objects.size());
        info->first_frame = source.firstFrame;
        info->last_frame = source.lastFrame;
        info->frame = source.currentFrame;
        info->camera = source.currentCamera;
        info->camera_count = source.cameraCount;
        return 1;
    }
    int marcus_object(MarcusPlayer *player, int index, MarcusObject *object)
    {
        return Guard(player, [&](ThreeMM::Error &) {
            if (!object || index < 0 || static_cast<std::size_t>(index) >= player->objects.size())
                return false;
            *object = {};
            const auto &source = player->objects[index];
            const auto name = player->names.find(source.actorId);
            Copy(object->name, name != player->names.end() ? name->second
                                                           : std::string(source.text   ? "3D text "
                                                                         : source.prop ? "Prop "
                                                                                       : "Actor ") +
                                                                 std::to_string(source.actorId));
            object->chunk = source.asset.number;
            object->template_chunk = source.templateAsset.number;
            object->actor_id = source.actorId;
            object->source_id = source.sourceId;
            object->first_frame = source.firstFrame;
            object->last_frame = source.lastFrame;
            object->action = source.action;
            object->cel = source.cel;
            object->prop = source.prop;
            object->text = source.text;
            object->visible = source.visible;
            object->has_lifetime = source.hasLifetime;
            object->x = source.x;
            object->y = source.y;
            object->z = source.z;
            return true;
        });
    }
    int marcus_seek(MarcusPlayer *player, long frame)
    {
        return Guard(player, [&](ThreeMM::Error &error) {
            if (player->selected < 0 || frame < (std::numeric_limits<std::int32_t>::min)() ||
                frame > (std::numeric_limits<std::int32_t>::max)())
                return false;
            if (!player->preview.SetFrame(static_cast<std::int32_t>(frame), error))
                return false;
            player->objects = player->preview.Objects();
            return true;
        });
    }
    int marcus_render(MarcusPlayer *player, int width, int height, int authored, float yaw, float pitch, float zoom,
                      MarcusImage *image)
    {
        if (image)
            *image = {};
        return Guard(player, [&](ThreeMM::Error &error) {
            if (!image || player->selected < 0)
                return false;
            ThreeMM::PreviewView view;
            view.width = width;
            view.height = height;
            view.authoredCamera = authored != 0;
            view.yaw = yaw;
            view.pitch = pitch;
            view.zoom = zoom;
            return Render(player, view, image, error);
        });
    }
    int marcus_render_fly(MarcusPlayer *player, int width, int height, const MarcusCamera *camera, MarcusImage *image)
    {
        // Allow a caller to reuse the camera record returned in this image.
        const MarcusCamera pose = camera ? *camera : MarcusCamera{};
        if (image)
            *image = {};
        return Guard(player, [&](ThreeMM::Error &error) {
            if (!camera)
                return false;
            ThreeMM::PreviewView view;
            view.width = width;
            view.height = height;
            view.authoredCamera = false;
            view.freeCamera = true;
            view.cameraX = pose.x;
            view.cameraY = pose.y;
            view.cameraZ = pose.z;
            view.yaw = pose.yaw;
            view.pitch = pose.pitch;
            return Render(player, view, image, error);
        });
    }
}
