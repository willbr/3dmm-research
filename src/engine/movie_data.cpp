/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "movie_data.h"
#include "engine_movie_graph.h"
#include "handle_services.h"
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
} // namespace

MMMovieSessionBinding MovieData::_SessionBinding()
{
    return {
        [](void *p, MMMovieSessionGraph *graph) {
            *graph = mm_movie_owner_graph(static_cast<PChunkyResourceFile>(p)->ReadOwner());
        },
        MovieOpenError};
}

MMRollCallBinding MovieData::_RollCallBinding()
{
    return {
        [](void *p) { return static_cast<PChunkyResourceFile>(p)->CoreCache(); },
        KauaiDecodeBlockHandle,
        {nullptr,
         [](void *, const void *input, size_t bytes, int encoding, void *output, size_t capacity) -> size_t {
             return CchTranslateRgb(const_cast<void *>(input), bytes, encoding, static_cast<achar *>(output), capacity);
         },
         &TagManager::CoreServices()},
        [](void *) { PushErc(ercSocBadFile); }};
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
        [](void *) -> void * {
            Filename name;
            if (name.FGetTemp())
                return ChunkyResourceFile::PcrfOpenWorking(&name, true);
            PushErc(ercCflCreate);
            return nullptr;
        },
        [](void *, void *path) -> void * {
            return ChunkyResourceFile::PcrfOpenWorking(static_cast<PFilename>(path), false);
        },
        _SessionBinding(),
        [](void *p) { static_cast<PChunkyResourceFile>(p)->Release(); },
        [](void *p) { return static_cast<PChunkyResourceFile>(p)->ArchiveWriter(); },
        _RollCallBinding(),
        [](void *) { Bug("chunk or child link missing"); },
        ChunkyResourceFile::ResourceTitleIO(),
        [](void *c, void *p) -> int {
            auto reader = static_cast<PChunkyResourceFile>(p)->ReadOwner();
            return KauaiSaveChunkOwner(reader->storage, kctgSoc, static_cast<NativeMovieHost *>(c)->filename);
        },
        [](void *p) {
            auto reader = static_cast<PChunkyResourceFile>(p)->ReadOwner();
            static_cast<PFileObject>(reader->storage->owner->files.main_file)->SetTemp(fFalse);
        }};
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
    const auto binding = _RollCallBinding();
    return mm_movie_load_bound_rollcall(resources, number, actors, limit, &binding) != 0;
}
bool MovieData::FWriteRollCall(PChunkyFile file, ChunkNumber number, MMRollCallTable *actors, ChunkNumber *written)
{
    const auto archive = file->WriteIndex();
    return mm_movie_write_rollcall(&archive, number, actors, written) != 0;
}
