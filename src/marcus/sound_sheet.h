#ifndef MARCUS_SOUND_SHEET_H
#define MARCUS_SOUND_SHEET_H

#include "mm/movie_sound_schedule.h"
#include "player.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MarcusSoundSheetTrackName;
struct MarcusSoundSheetCueNames;
typedef struct MarcusSoundSheet
{
    MMMovieSoundSheet model;
    struct MarcusSoundSheetTrackName *tracks;
    struct MarcusSoundSheetCueNames *cues;
} MarcusSoundSheet;

typedef struct MarcusSoundSheetNames
{
    void *context;
    const char *(*actor)(void *, MMI32 id);
    /* Return success with a NULL name when the archive has no title. */
    int (*sound)(void *, const MMMovieTimingCue *, const char **name);
} MarcusSoundSheetNames;

enum { MARCUS_SOUND_SHEET_FAILED, MARCUS_SOUND_SHEET_OK, MARCUS_SOUND_SHEET_MEMORY };
/* Initialize to zero. Build replaces the entire sheet only on success.
 * Name callbacks borrow UTF-8 strings through the next callback. They must
 * not reenter or unwind through this operation. */
int marcus_sound_sheet_build(MarcusSoundSheet *, const MMMovieSoundSchedule *, size_t scene,
                             const MarcusSoundSheetNames *);
void marcus_sound_sheet_clear(MarcusSoundSheet *);
const MMMovieSoundSheetCue *marcus_sound_sheet_cue(const MarcusSoundSheet *, int index);
int marcus_sound_sheet_sound(const MarcusSoundSheet *, int index, MarcusSound *);
int marcus_sound_sheet_track(const MarcusSoundSheet *, int index, MarcusSoundTrack *);
/* Last scene-level cue at the given sheet frame and style, or -1. */
int marcus_sound_sheet_last(const MarcusSoundSheet *, long frame, int style);

#ifdef __cplusplus
}
#endif
#endif
