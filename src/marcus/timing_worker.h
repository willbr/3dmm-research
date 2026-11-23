#ifndef MARCUS_TIMING_WORKER_H
#define MARCUS_TIMING_WORKER_H

#include "player.h"
#include "mm/movie_publish.h"
#include "mm/movie_sound_schedule.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusTimingWorker MarcusTimingWorker;

typedef struct MarcusTimingInput
{
    const char *movie, *resources, *content, *fonts, *origin;
    const MarcusScene *scenes;
    size_t count;
    int rate, modifier;
} MarcusTimingInput;

typedef struct MarcusTimingFiles
{
    void *context;
    /* OK reserves a private directory and lends its UTF-8 output path
     * until discard. Callbacks run on the worker and must not unwind. */
    MMMovieStageResult (*reserve)(void *, const char *directory, const char *leaf, const char **output);
    void (*discard)(void *);
} MarcusTimingFiles;

/* Copy the scene identities. Borrow the path strings and file services until
 * close. An allocation failure leaves *failure null. */
MarcusTimingWorker *marcus_timing_worker_start(const MarcusTimingInput *, const MarcusTimingFiles *, const char **failure);
/* The owner calls take once after completion. Success transfers the validated
 * schedule or reports a terminal diagnostic. Pending leaves both outputs alone. */
int marcus_timing_worker_take(MarcusTimingWorker *, MMMovieSoundSchedule **, const char **failure);
/* Cancel, reap the child and join the worker before releasing inputs. */
void marcus_timing_worker_close(MarcusTimingWorker *);

#ifdef __cplusplus
}
#endif
#endif
