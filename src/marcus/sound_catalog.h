#ifndef MARCUS_SOUND_CATALOG_H
#define MARCUS_SOUND_CATALOG_H

#include "player.h"
#include "mm/private_archive.h"
#include "mm/text.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusSoundCatalog
{
    MarcusSoundAsset *rows;
    char *directory;
    int count;
} MarcusSoundCatalog;

void marcus_sound_catalog_clear(MarcusSoundCatalog *);
int marcus_sound_catalog_matches(const MarcusSoundCatalog *, const char *directory);
const MarcusSoundAsset *marcus_sound_catalog_asset(const MarcusSoundCatalog *, int index);

/* Start with a zeroed catalog. Loading only publishes after the entire read
 * and sort succeed. An empty catalog is eligible for reload. */
enum { MARCUS_SOUND_CATALOG_OK, MARCUS_SOUND_CATALOG_OPEN, MARCUS_SOUND_CATALOG_MEMORY };
int marcus_sound_catalog_load(MarcusSoundCatalog *, const char *directory, const char *path,
                              const MMPrivateArchiveProfile *, const MMTextHost *);

#ifdef __cplusplus
}
#endif
#endif
