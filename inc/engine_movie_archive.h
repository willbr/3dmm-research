#ifndef ENGINE_MOVIE_ARCHIVE_H
#define ENGINE_MOVIE_ARCHIVE_H
#include "soc_core.h"
#include "movie_archive_host.h"
#include "mm/movie_archive_edit.h"

struct MovieEmbeddedArchive
{
    PChunkyFile destination;
    PChunkyFile source; // Null selects the live tag's cache.

    MMMovieEmbeddedArchive Services()
    {
        return {this,
            [](void *value) { return static_cast<MovieEmbeddedArchive *>(value)->destination->ReadChunkAccess(); },
            [](void *value, const MMTag *tag, MMU32 *number, int *created) -> int {
                auto &archive = *static_cast<MovieEmbeddedArchive *>(value);
                auto file = archive.source;
                if (!file && tag->cache)
                    file = static_cast<PChunkyResourceFile>(tag->cache)->Pcfl();
                return file && file->FFind(tag->type, tag->number) &&
                       file->FCopyReference(tag->type, tag->number, archive.destination, number, created);
            },
            destination->EditIndex()};
    }
};
#endif
