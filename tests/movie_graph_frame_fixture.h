#ifndef MOVIE_GRAPH_FRAME_FIXTURE_H
#define MOVIE_GRAPH_FRAME_FIXTURE_H

#include "engine_movie_graph.h"
#include <memory>

namespace
{
struct GraphFrameFile
{
    PChunkyFile graph = nullptr;
    PFileObject file = nullptr;
    ~GraphFrameFile()
    {
        ReleasePpo(&graph);
        ReleasePpo(&file);
    }
    bool Open(char *path)
    {
        String text;
        Filename name;
        text.SetSzs(path);
        if (!name.FBuildFromPath(&text))
            return false;
        graph = ChunkyFile::PcflOpen(&name, fcflNil);
        file = FileObject::PfilOpen(&name);
        return graph && file;
    }
    MMReadStream Stream() const
    {
        return {file, static_cast<MMU32>(file->FpMac()),
            [](void *p, MMU32 offset, void *bytes, size_t count) -> int {
                return count <= 0x7fffffffUL && offset <= 0x7fffffffUL &&
                       static_cast<PFileObject>(p)->FReadRgb(bytes, static_cast<LegacyLong>(count), offset);
            }};
    }
};

struct GraphFrameContent
{
    std::vector<std::unique_ptr<GraphFrameFile>> archives;
    const GraphFrameFile *Find(const MMMovieAsset &asset) const
    {
        for (const auto &archive : archives)
            if (archive->graph->FFind(asset.type, asset.number))
                return archive.get();
        return nullptr;
    }
    static int Graph(void *p, const MMMovieAsset *asset, MMMovieSessionGraph *out)
    {
        const auto file = static_cast<GraphFrameContent *>(p)->Find(*asset);
        if (!file)
            return MM_MOVIE_ASSET_NOT_FOUND;
        *out = EngineMovieGraph::FromFile(file->graph);
        return MM_MOVIE_OK;
    }
    static int Stream(void *p, const MMMovieAsset *asset, MMReadStream *out)
    {
        const auto file = static_cast<GraphFrameContent *>(p)->Find(*asset);
        if (!file)
            return MM_MOVIE_ASSET_NOT_FOUND;
        *out = file->Stream();
        return MM_MOVIE_OK;
    }
};

bool EqualAsset(const MMMovieAsset &a, const MMMovieAsset &b)
{
    return a.source == b.source && a.type == b.type && a.number == b.number;
}
bool EqualMatrix(const MMActorMatrix &a, const MMActorMatrix &b)
{
    return std::memcmp(a.m, b.m, sizeof(a.m)) == 0;
}
bool EqualPoint(const MMActorPoint &a, const MMActorPoint &b)
{
    return a.dxr == b.dxr && a.dyr == b.dyr && a.dzr == b.dzr;
}
bool EqualActor(const MMMovieEvaluatedActor &a, const MMMovieEvaluatedActor &b)
{
    return a.number == b.number && a.header.actor_id == b.header.actor_id &&
           a.header.source_id == b.header.source_id && a.header.template_number == b.header.template_number &&
           a.state.onStage == b.state.onStage && a.state.action == b.state.action && a.state.cel == b.state.cel &&
           a.state.frame == b.state.frame && a.state.nextEvent == b.state.nextEvent &&
           EqualPoint(a.translation, b.translation) && EqualMatrix(a.orientation, b.orientation);
}
bool EqualBody(const MMMovieEvaluatedBody &a, const MMMovieEvaluatedBody &b)
{
    if (a.part_count != b.part_count || !EqualActor(a.actor, b.actor) || !EqualAsset(a.model_owner, b.model_owner))
        return false;
    for (MMU32 i = 0; i < a.part_count; ++i)
    {
        const auto &left = a.parts[i];
        const auto &right = b.parts[i];
        if (left.parent != right.parent || left.set != right.set || !EqualMatrix(left.matrix, right.matrix) ||
            !EqualAsset(left.model, right.model) || !EqualAsset(left.active_model, right.active_model) ||
            !EqualAsset(left.material, right.material) || !EqualAsset(left.material_owner, right.material_owner))
            return false;
    }
    return true;
}
bool EqualFrame(const MMMovieEvaluatedFrame &a, const MMMovieEvaluatedFrame &b)
{
    const auto &left = a.camera;
    const auto &right = b.camera;
    if (a.frame != b.frame || a.scene.number != b.scene.number || a.actor_count != b.actor_count ||
        !EqualAsset(left.background, right.background) || left.slot != right.slot || left.number != right.number ||
        left.images.present != (MM_BACKGROUND_HAS_COLOUR | MM_BACKGROUND_HAS_DEPTH) ||
        left.images.present != right.images.present || left.images.colour != right.images.colour ||
        left.images.depth != right.images.depth ||
        !EqualMatrix(left.data.matrix, right.data.matrix) || !EqualPoint(left.data.place, right.data.place) ||
        left.data.near_z != right.data.near_z || left.data.far_z != right.data.far_z || left.data.fov != right.data.fov)
        return false;
    for (MMU32 i = 0; i < a.actor_count; ++i)
        if (!EqualActor(a.actors[i], b.actors[i]) || !EqualBody(*a.bodies[i], *b.bodies[i]))
            return false;
    return true;
}

bool EqualMeshes(const MMMovieBodyMeshes &a, const MMMovieBodyMeshes &b)
{
    if (a.part_count != b.part_count || a.model_count != b.model_count)
        return false;
    for (MMU32 i = 0; i < a.part_count; ++i)
        if (a.parts[i].model_index != b.parts[i].model_index || !EqualMatrix(a.parts[i].world, b.parts[i].world))
            return false;
    for (MMU32 i = 0; i < a.model_count; ++i)
    {
        const auto &x = a.models[i];
        const auto &y = b.models[i];
        if (x.description.vertices != y.description.vertices || x.description.faces != y.description.faces ||
            x.description.radius != y.description.radius ||
            (x.description.vertices && std::memcmp(x.vertices, y.vertices,
                                        x.description.vertices * sizeof(*x.vertices)) != 0) ||
            (x.description.faces && std::memcmp(x.faces, y.faces, x.description.faces * sizeof(*x.faces)) != 0))
            return false;
    }
    return true;
}

bool CompareMeshes(MMMovieSession *native, MMMovieSession *wire, const MMMovieEvaluatedFrame &a,
                   const MMMovieEvaluatedFrame &b, GraphFrameContent &content)
{
    for (MMU32 i = 0; i < a.actor_count; ++i)
    {
        MMMovieBodyMeshes *x = nullptr, *y = nullptr;
        const int live = mm_movie_session_body_meshes_graph(native, a.bodies[i], GraphFrameContent::Graph, &content, &x);
        const int packed = mm_movie_session_body_meshes(wire, b.bodies[i], GraphFrameContent::Stream, &content, &y);
        const bool equal = live == MM_MOVIE_OK && packed == MM_MOVIE_OK && EqualMeshes(*x, *y);
        mm_movie_body_meshes_dispose(y);
        mm_movie_body_meshes_dispose(x);
        if (!equal)
            return false;
    }
    return true;
}

int GraphFrameFixture(int argc, char **argv)
{
    GraphFrameFile movie;
    GraphFrameContent content;
    if (argc < 4 || !movie.Open(argv[2]))
        return 1;
    for (int i = 3; i < argc; ++i)
    {
        auto archive = std::make_unique<GraphFrameFile>();
        if (!archive->Open(argv[i]))
            return 1;
        content.archives.push_back(std::move(archive));
    }
    MMMovieSession *native, *wire;
    const auto graph = EngineMovieGraph::FromFile(movie.graph);
    if (mm_movie_session_open_graph(&graph, nullptr, nullptr, &native) != MM_MOVIE_OK)
        return 1;
    const std::unique_ptr<MMMovieSession, decltype(&mm_movie_session_close)> nativeOwner(native, mm_movie_session_close);
    const MMChunkPolicy policy = {0, 0, kcbMaxDataStn};
    const auto stream = movie.Stream();
    if (mm_movie_session_open(&stream, &policy, &wire) != MM_MOVIE_OK)
        return 1;
    const std::unique_ptr<MMMovieSession, decltype(&mm_movie_session_close)> wireOwner(wire, mm_movie_session_close);
    MMMovieSessionScene scene;
    MMU32 cursor = 0, slot, snapshots = 0;
    int status;
    while ((status = mm_movie_session_next_scene(native, nullptr, &cursor, &scene, &slot)) == MM_MOVIE_OK)
    {
        for (MMI32 frame = scene.first_frame; frame <= scene.last_frame && frame < 31; ++frame)
        {
            MMMovieEvaluatedFrame *a = nullptr, *b = nullptr;
            const int live = mm_movie_session_seek_graph(native, slot, frame, GraphFrameContent::Graph, &content, &a);
            const int packed = mm_movie_session_seek_with_bodies(wire, slot, frame, GraphFrameContent::Stream, &content, &b);
            const bool equal = live == MM_MOVIE_OK && packed == MM_MOVIE_OK &&
                               EqualFrame(*a, *b) && CompareMeshes(native, wire, *a, *b, content);
            mm_movie_frame_dispose(b);
            mm_movie_frame_dispose(a);
            if (!equal)
            {
                std::fprintf(stderr, "Graph frame mismatch at scene %lu frame %ld (graph %d, stream %d)\n",
                             static_cast<unsigned long>(slot), static_cast<long>(frame), live, packed);
                return 1;
            }
            ++snapshots;
        }
    }
    if (status != MM_MOVIE_SCENE_NOT_FOUND || !snapshots)
        return 1;
    std::printf("%lu native graph and stream frames match\n", static_cast<unsigned long>(snapshots));
    return 0;
}
}
#endif
