// Licensed under the MIT License.
#include "pack_archive.h"
#include <fstream>
#include <limits>

namespace StudioPacks
{
MMChunkArchiveResult CheckArchive(const std::filesystem::path &path, MMU32 maxPackedBytes, bool allowEmpty)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const std::streamoff length = file.tellg();
    if (length < 0 || length > std::numeric_limits<MMU32>::max())
        return MM_CHUNK_ARCHIVE_INDEX;
    const MMReadStream stream{&file, static_cast<MMU32>(length),
        [](void *context, MMU32 offset, void *bytes, std::size_t count) -> int {
            auto &input = *static_cast<std::ifstream *>(context);
            return bool(input.seekg(offset).read(static_cast<char *>(bytes), count));
        }};
    // Compact Kauai runtime and a 255-unit counted UTF-16 name.
    const unsigned short one = 1;
    const MMChunkPolicy policy{0, *reinterpret_cast<const unsigned char *>(&one) == 0, 516};
    return mm_chunk_archive_validate(&stream, &policy, maxPackedBytes, allowEmpty);
}
}
