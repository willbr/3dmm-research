// Licensed under the MIT License.
#include "pack_archive.h"
#include "engine_archive_stream.h"
#include <fstream>

namespace StudioPacks
{
MMChunkArchiveResult CheckArchive(const std::filesystem::path &path, MMU32 maxPackedBytes, bool allowEmpty)
{
    std::ifstream file(path, std::ios::binary);
    MMReadStream stream;
    if (!EngineArchiveStream::FromFile(file, stream))
        return MM_CHUNK_ARCHIVE_INDEX;
    const auto policy = EngineArchiveStream::CompactPolicy();
    return mm_chunk_archive_validate(&stream, &policy, maxPackedBytes, allowEmpty);
}
}
