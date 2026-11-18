// Licensed under the MIT License.
#ifndef ENGINE_ARCHIVE_STREAM_H
#define ENGINE_ARCHIVE_STREAM_H

#include "mm/chunky_file.h"
#include <istream>
#include <limits>

namespace EngineArchiveStream
{
inline MMChunkPolicy CompactPolicy()
{
    // Compact Kauai runtime and a 255-unit counted UTF-16 name.
    const unsigned short one = 1;
    return {0, *reinterpret_cast<const unsigned char *>(&one) == 0, 516};
}

inline bool FromFile(std::istream &input, MMReadStream &stream)
{
    const std::streamoff length = input.seekg(0, std::ios::end).tellg();
    if (length < 0 || length > std::numeric_limits<MMU32>::max())
        return false;
    stream = {&input, static_cast<MMU32>(length),
        [](void *context, MMU32 offset, void *bytes, std::size_t count) -> int {
            auto &file = *static_cast<std::istream *>(context);
            return bool(file.seekg(offset).read(static_cast<char *>(bytes), static_cast<std::streamsize>(count)));
        }};
    return true;
}
}
#endif
