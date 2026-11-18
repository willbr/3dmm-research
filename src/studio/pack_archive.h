// Licensed under the MIT License.
#ifndef STUDIO_PACK_ARCHIVE_H
#define STUDIO_PACK_ARCHIVE_H

#include "mm/chunk_storage.h"
#include <filesystem>

namespace StudioPacks
{
MMChunkArchiveResult CheckArchive(const std::filesystem::path &, MMU32 maxPackedBytes, bool allowEmpty);
}

#endif
