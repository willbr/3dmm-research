#ifndef KAUAI_MOVIE_ARCHIVE_HOST_H
#define KAUAI_MOVIE_ARCHIVE_HOST_H
#include "kauai_core.h"
#include "mm/movie_archive_edit.h"

struct MovieArchiveHost
{
    PChunkyFile file;

    explicit MovieArchiveHost(PChunkyFile file) : file(file), _binding{}
    {
        const auto archive = file->WriteIndex();
        const auto titles = KauaiChunkTitleIO();
        mm_movie_archive_file_init(&_binding, file, &archive, &titles);
    }
    ~MovieArchiveHost() { mm_movie_archive_file_clear(&_binding); }
    MovieArchiveHost(const MovieArchiveHost &) = delete;
    MovieArchiveHost &operator=(const MovieArchiveHost &) = delete;

    MMMovieArchiveEditor Editor()
    {
        return mm_movie_archive_file_editor(&_binding, sizeof(achar),
            [](void *, const void *bytes, size_t count, int encoding, void *text, size_t capacity) -> size_t {
                return CchTranslateRgb(const_cast<void *>(bytes), count, encoding, static_cast<achar *>(text), capacity);
            });
    }

    MMOwnedBlock &OutputBlock() { return _binding.output; }

  private:
    MMMovieArchiveFile _binding;
};

#endif
