#ifndef ENGINE_MOVIE_HOST_H
#define ENGINE_MOVIE_HOST_H

#include "handle_services.h"
#include "movie_archive_host.h"
#include "resource_archive_host.h"
#include "socdef.h"
#include "tagman.h"
#include "mm/movie_owner.h"
#include "mm/movie_session.h"

namespace EngineMovieHost
{
inline void ReportOpenFailure(void *, int status)
{
    ASSERTNAME
    PushErc(status == MM_MOVIE_BAD_VERSION ? ercSocBadVersion : ercSocBadFile);
}

inline MMRollCallBinding RollCallBinding()
{
    return {nullptr, KauaiDecodeBlockHandle, {nullptr, KauaiArchiveConvertText, &TagManager::CoreServices()},
            [](void *) {
                ASSERTNAME
                PushErc(ercSocBadFile);
            }};
}

inline MMMovieOwnerServices OwnerServices()
{
    MMMovieOwnerServices services = {};
    services.actor_allocator = CollectionAllocator();
    services.actor_encoding = koskCur;
#ifdef DEBUG
    services.actor_trash = 1;
#endif
    services.open_resources = [](void *, void *path) -> void * {
        return Chunky::OpenWorkingResourceCache(static_cast<PFilename>(path), false);
    };
    services.session.open_failed = ReportOpenFailure;
    services.rollcall = RollCallBinding();
    services.titles = mm_resource_cache_title_io(KauaiChunkOwnerTitleIO());
    return services;
}
} // namespace EngineMovieHost

#endif
