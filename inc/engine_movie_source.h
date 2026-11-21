#ifndef ENGINE_MOVIE_SOURCE_H
#define ENGINE_MOVIE_SOURCE_H

#include "kauai_core.h"
#include "mm/movie_session.h"

namespace EngineMovieSource
{
inline size_t ConvertWidth(void *, const void *bytes, size_t size, int encoding, void *text, size_t capacity)
{
    return CchTranslateRgb(const_cast<void *>(bytes), size, static_cast<short>(encoding),
                           static_cast<achar *>(text), capacity);
}

inline void ToString(const MMMovieSourceName &row, String &out)
{
    achar text[kcchMaxStn];
    const auto count = mm_movie_source_name_text(&row, koskCur, sizeof(achar), ConvertWidth, nullptr, text, kcchMaxStn);
    out.SetRgch(text, count);
}
} // namespace EngineMovieSource
#endif
