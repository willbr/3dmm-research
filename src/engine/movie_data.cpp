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
void MovieOpenError(void *, int status)
{
    PushErc(status == MM_MOVIE_BAD_VERSION ? ercSocBadVersion : ercSocBadFile);
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
const MMRollCallReader kRollCallReader = {
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
} // namespace

MMMovieSessionBinding MovieData::_SessionBinding()
{
    return {
        [](void *file, MMMovieSessionGraph *graph) { *graph = EngineMovieGraph::FromFile(static_cast<PChunkyFile>(file)); },
        MovieOpenError};
}

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
        _SessionBinding(),
        [](void *, void *file) -> void * { return ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(file), 0); },
        [](void *p) { static_cast<PChunkyFile>(p)->Release(); },
        [](void *p) { static_cast<PChunkyResourceFile>(p)->Release(); },
        [](void *p) -> void * { return static_cast<PChunkyResourceFile>(p)->Pcfl(); },
        [](void *file) { return static_cast<PChunkyFile>(file)->WriteIndex(); },
        [](void *p) { return static_cast<PChunkyResourceFile>(p)->CoreCache(); },
        kRollCallReader,
        KauaiDecodeBlockHandle,
        [](void *) { PushErc(ercSocBadFile); },
        [](void *) { Bug("chunk or child link missing"); },
        KauaiChunkTitleIO(),
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
    const auto graph = EngineMovieGraph::FromFile(file);
    const int status = mm_movie_graph_root(&graph, nullptr, number, header);
    if (status != MM_MOVIE_OK)
        MovieOpenError(file, status);
    return status == MM_MOVIE_OK;
}

bool MovieData::FLoadRollCall(PChunkyResourceFile resources, ChunkNumber number, MMRollCallTable *actors, MMI32 *limit)
{
    const int status = mm_movie_load_rollcall(resources->CoreCache(), number, resources, actors, limit,
                                              KauaiDecodeBlockHandle, &kRollCallReader);
    if (status == MM_CHUNK_NOT_FOUND)
        PushErc(ercSocBadFile);
    return status == MM_CHUNK_OK;
}
bool MovieData::FWriteRollCall(PChunkyFile file, ChunkNumber number, MMRollCallTable *actors, ChunkNumber *written)
{
    const auto archive = file->WriteIndex();
    return mm_movie_write_rollcall(&archive, number, actors, written) != 0;
}
