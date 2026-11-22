#ifndef MARCUS_ASSET_CATALOG_H
#define MARCUS_ASSET_CATALOG_H

#include "player.h"
#include "mm/private_archive.h"
#include "mm/text.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MarcusAssetKind
{
    MARCUS_ASSET_PROP, MARCUS_ASSET_ACTOR, MARCUS_ASSET_FONT, MARCUS_ASSET_MATERIAL,
    MARCUS_ASSET_BACKGROUND, MARCUS_ASSET_KIND_COUNT
} MarcusAssetKind;

typedef struct MarcusAssetCatalog
{
    MarcusProp *rows;
    char *directory;
    int count;
} MarcusAssetCatalog;

const char *marcus_asset_catalog_file(MarcusAssetKind);
/* Start with a zeroed catalog. Failed loads leave the previous rows intact.
 * Returns an MMArchiveReadResult. */
int marcus_asset_catalog_load(MarcusAssetCatalog *, MarcusAssetKind, const char *directory, const char *path,
                              const MMPrivateArchiveProfile *, const MMTextHost *);
void marcus_asset_catalog_clear(MarcusAssetCatalog *);
int marcus_asset_catalog_matches(const MarcusAssetCatalog *, const char *directory);
const MarcusProp *marcus_asset_catalog_row(const MarcusAssetCatalog *, int index);
const MarcusProp *marcus_asset_catalog_find(const MarcusAssetCatalog *, unsigned long chunk);

#ifdef __cplusplus
}
int MarcusLoadAssetCatalog(MarcusAssetCatalog &, MarcusAssetKind, const char *directory);
#endif
#endif
