#ifndef MOVIE_STATE_FIXTURE_H
#define MOVIE_STATE_FIXTURE_H

#include "engine_movie_host.h"

// Detached state for tests of the C owner and real native archive files.
struct MovieStateFixture : MMMovieState
{
    MovieStateFixture() { mm_movie_state_init(this, sizeof(achar)); }
    ~MovieStateFixture()
    {
        const auto services = EngineMovieHost::OwnerServices();
        mm_movie_state_dispose(this, &services);
    }
    MovieStateFixture(const MovieStateFixture &) = delete;
    MovieStateFixture &operator=(const MovieStateFixture &) = delete;

    static void CopyTitle(const MMMovieState &movie, String &out)
    {
        const auto text = static_cast<const achar *>(mm_movie_title_text(&movie.title));
        out.SetRgch(const_cast<achar *>(text), movie.title.count);
    }

    static MMMovieOwnerServices FileServices(PFilename destination)
    {
        auto services = EngineMovieHost::OwnerServices();
        services.context = destination;
        services.create_resources = [](void *) -> void * {
            Filename name;
            if (name.FGetTemp())
                return Chunky::OpenWorkingResourceCache(&name, true);
            ASSERTNAME
            PushErc(ercCflCreate);
            return nullptr;
        };
        services.save = [](void *path, void *cache) -> int {
            const auto reader = mm_resource_cache_reader(static_cast<MMResourceCache *>(cache));
            return KauaiSaveChunkOwner(reader->storage, kctgSoc, static_cast<PFilename>(path));
        };
        services.make_persistent = [](void *cache) {
            const auto reader = mm_resource_cache_reader(static_cast<MMResourceCache *>(cache));
            static_cast<PFileObject>(reader->storage->owner->files.main_file)->SetTemp(fFalse);
        };
        return services;
    }
};

inline bool CreateMovieFixtureRoot(PChunkyFile file, ChunkNumber *number)
{
    const auto archive = file->WriteIndex();
    return mm_movie_create_root(&archive, number) != 0;
}

#endif
