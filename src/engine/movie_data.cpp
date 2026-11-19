/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "movie_data.h"
#include "engine_movie_graph.h"
#include "engine_read_stream.h"
#include "mm/movie_rollcall.h"
#include "mm/movie_session.h"
ASSERTNAME

namespace
{
struct NativeMovieHost
{
    PFilename filename;
};
int ReadMovieRecord(void *context, MMU32 position, const MMChunkEntry *entry, void *bytes, MMU32 count)
{
    return EngineMovieGraph::Read(context, nullptr, position, entry, bytes, count);
}
bool CheckMovieOpen(int result)
{
    if (result == MM_MOVIE_OK)
        return true;
    PushErc(result == MM_MOVIE_BAD_VERSION ? ercSocBadVersion : ercSocBadFile);
    return false;
}
TAG ActorTag(const MMRollCallActor &actor)
{
    TAG tag;
    TagFromCore(&tag, mm_movie_rollcall_tag(&actor));
    return tag;
}
void CloseActor(void *, MMRollCallActor *a)
{
    auto tag = ActorTag(*a);
    TagManager::CloseTag(&tag);
    a->cache = tag.pcrf;
}
} // namespace

MMMovieOwnerServices MovieData::_OwnerServices(void *context)
{
    return {
        context,
        CollectionAllocator(),
        koskCur,
#ifdef DEBUG
        1,
#else
        0,
#endif
        [](void *) -> void * { return ChunkyFile::PcflCreateTemp(); },
        [](void *c) -> void * { return ChunkyFile::PcflOpen(static_cast<NativeMovieHost *>(c)->filename, fcflNil); },
        [](void *, void *file, MMU32 *number, MMMovieSession **out) -> int {
            const auto graph = EngineMovieGraph::FromFile(static_cast<PChunkyFile>(file));
            return CheckMovieOpen(mm_movie_session_open_graph(&graph, nullptr, number, out));
        },
        [](void *, void *file) -> void * { return ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(file), 0); },
        [](void *p) { static_cast<PChunkyFile>(p)->Release(); },
        [](void *p) { static_cast<PChunkyResourceFile>(p)->Release(); },
        CloseActor,
        [](void *p) -> void * { return static_cast<PChunkyResourceFile>(p)->Pcfl(); },
        [](void *, void *file, MMU32 *number) -> int {
            return FCreateRoot(static_cast<PChunkyFile>(file), number);
        },
        [](void *, void *resources, MMU32 number, MMRollCallTable *actors, MMI32 *limit) -> int {
            return FLoadRollCall(static_cast<PChunkyResourceFile>(resources), number, actors, limit);
        },
        KauaiChunkTitleIO(),
        [](void *, void *file, MMU32 movie, MMU32 *previous) -> int {
            ChildChunkIdentification child;
            const bool found = static_cast<PChunkyFile>(file)->FGetKidChidCtg(kctgMvie, movie, 0, kctgGst, &child);
            if (found)
                *previous = child.cki.cno;
            return found;
        },
        [](void *, void *file, MMU32 movie, MMRollCallTable *actors, MMU32 *written) -> int {
            return FWriteRollCall(static_cast<PChunkyFile>(file), movie, actors, written);
        },
        [](void *, void *file, MMU32 movie, MMU32 previous) {
            static_cast<PChunkyFile>(file)->DeleteChild(kctgMvie, movie, kctgGst, previous, 0);
        },
        [](void *c, void *file) -> int {
            return static_cast<PChunkyFile>(file)->FSave(kctgSoc, static_cast<NativeMovieHost *>(c)->filename);
        },
        [](void *file) { static_cast<PChunkyFile>(file)->SetTemp(fFalse); }};
}
MovieData::~MovieData()
{
    const auto s = _OwnerServices(nullptr);
    mm_movie_state_dispose(this, &s);
}
void MovieData::ClearActors()
{
    const auto s = _OwnerServices(nullptr);
    mm_movie_clear_actors(this, &s);
}
bool MovieData::FInitActors()
{
    const auto s = _OwnerServices(nullptr);
    return mm_movie_init_actors(this, &s) != 0;
}
void MovieData::ReleaseResources()
{
    const auto s = _OwnerServices(nullptr);
    mm_movie_release_resources(this, &s);
}
void MovieData::GetTitle(PString out) const
{
    out->SetRgch(const_cast<achar *>(static_cast<const achar *>(mm_movie_title_text(&title))), title.count);
}
void MovieData::SetTitle(PString value)
{
    AssertDo(mm_movie_title_set(&title, value->Prgch(), value->Cch()), "bad movie title");
}
bool MovieData::FInitEmpty()
{
    const auto s = _OwnerServices(nullptr);
    return mm_movie_init_empty(this, &s) != 0;
}
bool MovieData::FLoad(PFilename filename, ChunkNumber number)
{
    NativeMovieHost host = {filename};
    const auto s = _OwnerServices(&host);
    return mm_movie_load(this, number, &s) != 0;
}
bool MovieData::FResolveScene(ChildChunkID slot, ChunkNumber *number)
{
    const auto s = _OwnerServices(nullptr);
    return mm_movie_state_scene_number(this, slot, number, &s) != 0;
}
bool MovieData::FSave(PFilename filename)
{
    NativeMovieHost host = {filename};
    const auto s = _OwnerServices(&host);
    return mm_movie_save(this, &s) != 0;
}
namespace
{
struct NativeMovieRootHost
{
    PChunkyFile file;
    DataBlock block;
};
MMMovieRootServices MovieRootServices(NativeMovieRootHost *host)
{
    return {
        host,
        nullptr,
        nullptr,
        [](void *c, MMU32 bytes, MMU32 *number) -> int {
            auto h = static_cast<NativeMovieRootHost *>(c);
            return h->file->FAdd(bytes, kctgMvie, number, &h->block);
        },
        [](void *c, const void *bytes, MMU32 count) -> int {
            return static_cast<NativeMovieRootHost *>(c)->block.FWriteRgb(const_cast<void *>(bytes), count, 0);
        },
        [](void *c, MMU32 number) { static_cast<NativeMovieRootHost *>(c)->file->Delete(kctgMvie, number); },
        nullptr};
}
} // namespace
bool MovieData::FCreateRoot(PChunkyFile file, ChunkNumber *number)
{
    NativeMovieRootHost host = {file};
    const auto s = MovieRootServices(&host);
    const unsigned short one = 1;
    return mm_movie_create_root(number, koskCur, *reinterpret_cast<const byte *>(&one) == 0, &s) != 0;
}
bool MovieData::FVerifyVersion(PChunkyFile file, ChunkNumber *number, MMMovieHeader *header)
{
    const MMMovieChunkView view = {file->ReadChunkAccess(), file, ReadMovieRecord};
    return CheckMovieOpen(mm_movie_view_root(&view, number, header));
}

bool MovieData::FLoadRollCall(PChunkyResourceFile resources, ChunkNumber number, MMRollCallTable *actors, MMI32 *limit)
{
    auto file = resources->Pcfl();
    ChildChunkIdentification child;
    DataBlock block;
    if (!file->FGetKidChidCtg(kctgMvie, number, 0, kctgGst, &child) ||
        !file->FFind(child.cki.ctg, child.cki.cno, &block))
    {
        PushErc(ercSocBadFile);
        return false;
    }
    if (!block.FUnpackData())
        return false;
    const auto stream = EngineReadStream::FromBlock(block);
    const MMRollCallReader reader = {
        nullptr,
        [](void *, const void *input, size_t bytes, int encoding, void *output, size_t capacity) -> size_t {
            return CchTranslateRgb(const_cast<void *>(input), bytes, encoding, static_cast<achar *>(output), capacity);
        },
        [](void *, MMRollCallActor *a, void *resources) -> int {
            auto tag = ActorTag(*a);
            const bool ok = TagManager::FOpenTag(&tag, static_cast<PChunkyResourceFile>(resources));
            const auto value = TagToCore(tag);
            a->data.source_id = value.sid;
            a->data.template_type = value.type;
            a->data.template_number = value.number;
            a->cache = value.cache;
            return ok;
        },
        CloseActor};
    return mm_movie_read_rollcall(&stream, resources, actors, limit, &reader) != 0;
}
bool MovieData::FWriteRollCall(PChunkyFile file, ChunkNumber number, MMRollCallTable *actors, ChunkNumber *written)
{
    struct Output
    {
        PChunkyFile file;
        DataBlock block;
    } output = {file};
    const MMRollCallWriter writer = {
        &output,
        [](void *c, MMU32 bytes, MMU32 *n, MMStorageWriter *out) -> int {
            auto h = static_cast<Output *>(c);
            if (!h->file->FAdd(bytes, kctgGst, n, &h->block))
                return 0;
            *out = {&h->block, bytes, [](void *p, MMU32 offset, const void *data, size_t count) -> int {
                return static_cast<PDataBlock>(p)->FWriteRgb(const_cast<void *>(data), count, offset);
            }};
            return 1;
        },
        [](void *c, MMU32 movie, MMU32 n) -> int {
            return static_cast<Output *>(c)->file->FAdoptChild(kctgMvie, movie, kctgGst, n, 0);
        },
        [](void *c, MMU32 n) { static_cast<Output *>(c)->file->Delete(kctgGst, n); }};
    return mm_movie_write_rollcall(number, actors, written, &writer) != 0;
}
