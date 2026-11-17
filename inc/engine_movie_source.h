#ifndef ENGINE_MOVIE_SOURCE_H
#define ENGINE_MOVIE_SOURCE_H

#include "kauai_core.h"
#include "mm/movie_session.h"
#include "mm/string_ops.h"
#include <cstring>

namespace EngineMovieSource
{
// Preserve the native GST reader's character conversion for host source names.
inline void ToString(const MMMovieSourceName &row, String &out)
{
    achar text[kcchMaxStn];
    LegacyLong count;
    if (CbCharOsk(static_cast<short>(row.encoding)) == sizeof(achar))
    {
        count = row.size / sizeof(achar);
        std::memcpy(text, row.bytes, row.size);
        mm_string_translate(text, count, sizeof(achar), row.encoding, koskCur, fTrue);
    }
    else
        count = CchTranslateRgb(const_cast<unsigned char *>(row.bytes), row.size, static_cast<short>(row.encoding),
                                text, kcchMaxStn);
    out.SetRgch(text, count);
}
} // namespace EngineMovieSource
#endif
