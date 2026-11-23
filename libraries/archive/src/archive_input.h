#ifndef MM_ARCHIVE_INPUT_H
#define MM_ARCHIVE_INPUT_H

#include "mm/archive.h"
#include "mm/native_archive_file.h"

namespace MM {
namespace Internal {

// Bind Kauai paths, live archive lookup and typed caches to the C input.
class ArchiveInput
{
    MMNativeArchiveInput input = {};
    std::string path;

public:
    ArchiveInput() = default;
    ~ArchiveInput() { mm_native_archive_input_close(&input); }
    ArchiveInput(const ArchiveInput &) = delete;
    ArchiveInput &operator=(const ArchiveInput &) = delete;

    bool Open(const std::string &, Error &, int mode, const Options & = {}, bool checkLiveLinks = true);
    void Swap(ArchiveInput &) noexcept;
    bool CanPreview() const { return input.reader && input.mode != MM_NATIVE_ARCHIVE_STORED; }
    const std::string &Path() const { return path; }
    MMArchiveReader *Reader() const { return input.reader; }
    MMMovieSession *MovieSession() const { return mm_archive_reader_movie(input.reader); }
    MMMovieSessionGraph PlaybackGraph() const { return mm_native_archive_input_graph(&input); }
    MMResourceCache *NewResourceCache(MMI32 budget) const;
    std::string ChunkName(MMU32 type, MMU32 number) const;
};

bool ArchiveCatalogError(int status, const MMChunkEntry &, Error &);
std::string ArchiveTextUtf8(const void *units, std::size_t count);

} // namespace Internal
} // namespace MM
#endif
