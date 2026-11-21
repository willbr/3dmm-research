#ifndef MARCUS_ARCHIVE_FILE_H
#define MARCUS_ARCHIVE_FILE_H
#include "mm/private_archive.h"
#ifdef __cplusplus
extern "C" {
#endif

MMPrivateArchive *marcus_archive_open(const char *path, int mode, const MMPrivateArchiveProfile *);

#ifdef __cplusplus
}
#include <filesystem>
#include <memory>
using MarcusArchive = std::unique_ptr<MMPrivateArchive, decltype(&mm_private_archive_close)>;
MarcusArchive MarcusOpenArchive(const std::filesystem::path &, int mode);
class String;
MMChunkNameValue MarcusArchiveName(String &, void *wire);
#endif
#endif
