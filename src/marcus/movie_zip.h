/* Licensed under the MIT License. */
#ifndef MARCUS_MOVIE_ZIP_H
#define MARCUS_MOVIE_ZIP_H

#include "mm/zip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusZipEntry
{
    char *name;
    const char *extension;
    MMZipMember member;
    MMU32 slot;
} MarcusZipEntry;
typedef struct MarcusZipList
{
    MarcusZipEntry *items;
    MMU32 count, capacity;
} MarcusZipList;
typedef struct MarcusZipCatalog
{
    MarcusZipList movies, fonts;
} MarcusZipCatalog;

enum { MARCUS_ZIP_FONT_FILE_BYTES = 16 * 1024 * 1024 };
int marcus_zip_signature(const char *filename, const char signature[4]);
int marcus_zip_is_font(const char *filename);
/* Read an archive in directory order and retain the original slot of each
 * selected entry, including duplicate names. The result is untouched on error.
 * Pass an empty result. A non-null return is a diagnostic string. */
const char *marcus_zip_catalog_read(MMZip *, int (*cancelled)(void *), void *context, MarcusZipCatalog *);
void marcus_zip_catalog_clear(MarcusZipCatalog *);
const char *marcus_zip_check_movie(const MarcusZipEntry *);
int marcus_zip_check_fonts(const MarcusZipList *);
int marcus_zip_legacy_unambiguous(const MarcusZipList *, const MarcusZipEntry *);

#ifdef __cplusplus
}
#endif
#endif
