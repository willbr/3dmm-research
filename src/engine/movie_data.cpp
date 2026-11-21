/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "movie_data.h"
#include "engine_movie_graph.h"
#include "handle_services.h"
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
        [](void *, void *path) -> void * { return ChunkyFile::PcflOpen(static_cast<PFilename>(path), fcflNil); },
        OpenSession,
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
    const auto s = _OwnerServices(nullptr);
    return mm_movie_load(this, filename, number, &s) != 0;
}
int MovieData::OpenSession(void *, void *file, MMU32 *number, MMMovieSession **out)
{
    const auto graph = EngineMovieGraph::FromFile(static_cast<PChunkyFile>(file));
    return CheckMovieOpen(mm_movie_session_open_graph(&graph, nullptr, number, out));
}
bool MovieData::FSave(PFilename filename)
{
    NativeMovieHost host = {filename};
    const auto s = _OwnerServices(&host);
    return mm_movie_save(this, &s) != 0;
}
bool MovieData::FCreateRoot(PChunkyFile file, ChunkNumber *number)
{
    const auto archive = file->WriteIndex();
    return mm_movie_create_root(&archive, number) != 0;
}
bool MovieData::FVerifyVersion(PChunkyFile file, ChunkNumber *number, MMMovieHeader *header)
{
    const MMMovieChunkView view = {file->ReadChunkAccess(), file, ReadMovieRecord};
    return CheckMovieOpen(mm_movie_view_root(&view, number, header));
}

bool MovieData::FLoadRollCall(PChunkyResourceFile resources, ChunkNumber number, MMRollCallTable *actors, MMI32 *limit)
{
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
    const int status = mm_movie_load_rollcall(resources->CoreCache(), number, resources, actors, limit,
                                              KauaiDecodeBlockHandle, &reader);
    if (status == MM_CHUNK_NOT_FOUND)
        PushErc(ercSocBadFile);
    return status == MM_CHUNK_OK;
}
bool MovieData::FWriteRollCall(PChunkyFile file, ChunkNumber number, MMRollCallTable *actors, ChunkNumber *written)
{
    const auto archive = file->WriteIndex();
    return mm_movie_write_rollcall(&archive, number, actors, written) != 0;
}
