#ifndef KAUAI_MOVIE_ARCHIVE_HOST_H
#define KAUAI_MOVIE_ARCHIVE_HOST_H
#include "kauai_core.h"
#include "mm/movie_archive_edit.h"

inline size_t KauaiArchiveConvertText(void *, const void *input, size_t bytes, int encoding,
                                     void *output, size_t capacity)
{
    return CchTranslateRgb(const_cast<void *>(input), bytes, encoding, static_cast<achar *>(output), capacity);
}

inline MMMovieArchiveText KauaiMovieArchiveText()
{
    return {KauaiChunkOwnerTitleIO(), sizeof(achar), KauaiArchiveConvertText};
}

struct MovieArchiveHost
{
    explicit MovieArchiveHost(PChunkyFile file) : MovieArchiveHost(file->ReadOwner(), file->WriteOwnerIndex()) {}

    MovieArchiveHost(MMChunkOwnerRead *reader, const MMArchiveWriter &archive) : _binding{}
    {
        const auto titles = KauaiChunkOwnerTitleIO();
        mm_movie_archive_file_init(&_binding, reader, &archive, &titles);
    }
    ~MovieArchiveHost() { mm_movie_archive_file_clear(&_binding); }
    MovieArchiveHost(const MovieArchiveHost &) = delete;
    MovieArchiveHost &operator=(const MovieArchiveHost &) = delete;

    const MMArchiveWriter &Writer() const { return _binding.archive; }

    MMMovieArchiveEditor Editor()
    {
        return mm_movie_archive_file_editor(&_binding, sizeof(achar), KauaiArchiveConvertText);
    }

  private:
    MMMovieArchiveFile _binding;
};

#endif
