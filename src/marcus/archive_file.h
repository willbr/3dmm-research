#ifndef MARCUS_ARCHIVE_FILE_H
#define MARCUS_ARCHIVE_FILE_H
#include "mm/private_archive.h"
#ifdef __cplusplus
extern "C" {
#endif

MMPrivateArchive *marcus_archive_open(const char *path, int mode, const MMPrivateArchiveProfile *);

#ifdef __cplusplus
}
#endif
#endif
