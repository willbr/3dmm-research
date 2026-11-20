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
            [](void *value, MMU32 parent_type, MMU32 parent, const MMChunkChild *child, int clear_loner) -> int {
                auto file = static_cast<MovieEmbeddedArchive *>(value)->destination;
                return file->FAdoptChild(parent_type, parent, child->type, child->number, child->slot, clear_loner != 0);
            },
            [](void *value, MMU32 type, MMU32 number) {
                static_cast<MovieEmbeddedArchive *>(value)->destination->Delete(type, number);
            }};
    }
};
#endif
