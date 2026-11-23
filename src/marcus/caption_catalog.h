#ifndef MARCUS_CAPTION_CATALOG_H
#define MARCUS_CAPTION_CATALOG_H

#include "player.h"
#include "mm/caption_edit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusCaptionEntry
{
    MMU32 slot, chunk;
    MarcusCaptionOptions options;
    char *text;
} MarcusCaptionEntry;

struct MarcusCaptionList
{
    MarcusCaptionEntry *entries;
    char **font_names;
    int count, font_count;
    MarcusCaptionOptions defaults;
    unsigned long document;
    MMU32 scene;
    MMI32 first_frame, last_frame;
};

enum
{
    MARCUS_CAPTION_CATALOG_OK, MARCUS_CAPTION_CATALOG_ARCHIVE, MARCUS_CAPTION_CATALOG_BOX,
    MARCUS_CAPTION_CATALOG_EDIT, MARCUS_CAPTION_CATALOG_MEMORY, MARCUS_CAPTION_CATALOG_UTF8,
    MARCUS_CAPTION_CATALOG_TEXT_LIMIT, MARCUS_CAPTION_CATALOG_FONT_LIMIT,
    MARCUS_CAPTION_CATALOG_INPUT, MARCUS_CAPTION_CATALOG_NO_CHANGE, MARCUS_CAPTION_CATALOG_OPEN,
    MARCUS_CAPTION_CATALOG_SAVE
};

MarcusCaptionList *marcus_caption_catalog_create(unsigned long document, MMU32 scene, MMI32 first, MMI32 last,
                                                 MMI32 current);
typedef MMU32 MarcusCaptionColour(const void *, MMU32);
/* Publish all editor rows together. The palette provider may borrow the
 * current scene for the synchronous load. Failure retains the existing list. */
int marcus_caption_catalog_load(MarcusCaptionList *, struct MMPrivateArchive *, const MMTextHost *, int native_system,
                                const void *, MarcusCaptionColour *, int *detail);
const MarcusCaptionEntry *marcus_caption_catalog_entry(const MarcusCaptionList *, int index);

struct MarcusSceneRows;
struct MMCaptionPresentation;
int marcus_caption_catalog_current(const MarcusCaptionList *, unsigned long document, const struct MarcusSceneRows *,
                                    MMI32 selected);
int marcus_caption_catalog_hit(const MarcusCaptionList *, const struct MMCaptionPresentation *, MMI32 x, MMI32 y,
                               const MMU32 *hidden);

typedef struct MarcusCaptionTarget
{
    unsigned long document;
    MMI32 scene;
    MMU32 chunk;
    int kind;
} MarcusCaptionTarget;

/* Select an authored box, or publish the target of a prepared draft. A new
 * draft applies to the scene without hiding a box. Invalid selection clears
 * the target. The target copies the row identity and can outlive the list. */
void marcus_caption_target_set(MarcusCaptionTarget *, const MarcusCaptionList *, int index, unsigned long document,
                               const struct MarcusSceneRows *, MMI32 selected, int prepared_draft);
/* Return the authored box to hide and optionally report whether the target
 * is active. Chunk zero is a valid target. */
const MMU32 *marcus_caption_target_resolve(const MarcusCaptionTarget *, unsigned long document, MMI32 selected,
                                         int *editing);

int marcus_caption_catalog_valid_edit(const MarcusCaptionList *, int index, const MarcusCaptionOptions *,
                                      const char *text, int allow_delete);
void marcus_caption_catalog_input(MMCaptionEditInput *, const MarcusCaptionOptions *, const char *text);

typedef struct MarcusCaptionSaveHost
{
    void *context;
    /* Open a new private working copy and transfer its archive reference. */
    struct MMPrivateArchive *(*copy)(void *);
    const MMTextHost *encoding;
    MMU32 creator;
} MarcusCaptionSaveHost;

/* Validate and plan the edit before requesting a copy. The host keeps the
 * working file lease and publishes the file only after this returns OK. */
int marcus_caption_catalog_save(const MarcusCaptionList *, int index, const MarcusCaptionOptions *,
                                const char *text, const MarcusCaptionSaveHost *, int *detail);

typedef const char *MarcusCaptionFontRead(const void *, size_t index, size_t *bytes);
/* Borrow candidate names while filtering and sorting. The last spelling of
 * an ASCII case-insensitive duplicate wins. Publish only a complete list. */
int marcus_caption_catalog_fonts(MarcusCaptionList *, size_t count, const void *, MarcusCaptionFontRead *,
                                 const MMTextHost *, unsigned native_unit);

#ifdef __cplusplus
}
#endif
#endif
