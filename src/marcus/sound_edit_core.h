#ifndef MARCUS_SOUND_EDIT_CORE_H
#define MARCUS_SOUND_EDIT_CORE_H

#include "sound_sheet.h"
#include "mm/movie_sound_archive.h"
#include "mm/private_archive.h"
#include "mm/sound_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MARCUS_SOUND_EDIT_FAILED, MARCUS_SOUND_EDIT_READY, MARCUS_SOUND_EDIT_UNCHANGED };

typedef struct MarcusSoundEditPlan
{
    MMU32 scene;
    MMMovieSoundArchiveCue cue;
    MMMovieSoundArchiveEdit values; /* Pointers are bound by the writer. */
    int existing;
} MarcusSoundEditPlan;

/* Only READY publishes a plan. A missing cue returns FAILED without a message. */
int marcus_sound_edit_plan(const MarcusScene *scenes, size_t count, int selected,
                           const MarcusSoundSheet *sheet, int row, const MarcusSoundOptions *,
                           int remove, int importing, const MarcusSoundAsset *,
                           MarcusSoundEditPlan *, const char **message);

typedef struct MarcusSoundImport
{
    const char *path, *extension;
    MMChunkNameValue title;
    MMSoundWaveImport wave;
} MarcusSoundImport;

/* The caller owns the candidate and discards it on failure. NULL means success. */
const char *marcus_sound_edit_write(const char *candidate, const MMPrivateArchiveProfile *,
                                    const MarcusSoundEditPlan *, const MarcusSoundImport *,
                                    const char *library, MMU32 library_chunk);
const char *marcus_sound_preview_write(const char *candidate, const MMPrivateArchiveProfile *,
                                       const MarcusSoundImport *, int style);

#ifdef __cplusplus
}
#endif
#endif
