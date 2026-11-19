#ifndef MM_ENGINE_MOVIE_SOUND_SCHEDULE_H
#define MM_ENGINE_MOVIE_SOUND_SCHEDULE_H
#include "movie_timing_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMMovieSoundSchedule MMMovieSoundSchedule;

enum { MM_SOUND_CLIP_SAMPLED = 1, MM_SOUND_CLIP_MIDI = 2 };
enum { MM_SOUND_REFRAME_OK, MM_SOUND_REFRAME_MISMATCH, MM_SOUND_REFRAME_MEMORY };

typedef struct MMScheduledSound
{
    size_t scene, source_row;
    double start, end;
} MMScheduledSound;
typedef struct MMScheduledResume
{
    size_t scene, source_row;
    int queue;
    double seconds;
} MMScheduledResume;

typedef struct MMMovieSoundPosition
{
    size_t scene;
    MMI32 frame;
    int finished;
} MMMovieSoundPosition;

typedef struct MMMovieSoundTrack
{
    MMI32 actor, style;
    int actor_sound;
    size_t cue_count;
} MMMovieSoundTrack;
typedef struct MMMovieSoundSheetCue
{
    size_t track, origin_scene;
    MMI32 frame;
    int carried;
    double origin_frame, timeline_frame, end_frame;
} MMMovieSoundSheetCue;
typedef struct MMMovieSoundSheet
{
    MMMovieSoundTrack *tracks;
    MMMovieSoundSheetCue *cues;
    size_t track_count, cue_count;
} MMMovieSoundSheet;

MMMovieSoundSchedule *mm_movie_sound_schedule_create(double frame_seconds);
void mm_movie_sound_schedule_destroy(MMMovieSoundSchedule *);
void mm_movie_sound_schedule_clear(MMMovieSoundSchedule *);
void mm_movie_sound_schedule_rate(MMMovieSoundSchedule *, double fps);
double mm_movie_sound_schedule_frame_seconds(const MMMovieSoundSchedule *);
size_t mm_movie_sound_schedule_size(const MMMovieSoundSchedule *);

/* Append copies one scene's timing sources. The transition belongs to its
 * outgoing edge. Failure preserves all previously published scenes. */
int mm_movie_sound_schedule_append(MMMovieSoundSchedule *, const MMMovieTimingScene *);
/* Borrow the scene's inputs until the schedule is cleared or destroyed. */
void mm_movie_sound_schedule_source(const MMMovieSoundSchedule *, size_t scene, MMMovieTimingScene *);
/* Rebuild the common prefix using the new ranges and the stored inputs.
 * The previous schedule survives either result. Output is set on success. */
int mm_movie_sound_schedule_reframe(const MMMovieSoundSchedule *, const MMMovieTimingRange *, size_t count,
                                     MMMovieSoundSchedule **out);

double mm_movie_sound_schedule_time_at(const MMMovieSoundSchedule *, size_t scene, double frame);
double mm_movie_sound_schedule_frame_at(const MMMovieSoundSchedule *, size_t scene, double time);
/* Chapter start includes the incoming transition. A seek within the
 * transition resolves to the new scene's first frame. */
void mm_movie_sound_schedule_scene_time(const MMMovieSoundSchedule *, size_t scene, double *start, double *end);
void mm_movie_sound_schedule_movie_time(const MMMovieSoundSchedule *, size_t scene, MMI32 frame,
                                        double elapsed, int finished, double *duration, double *position);
int mm_movie_sound_schedule_locate(const MMMovieSoundSchedule *, double seconds, MMMovieSoundPosition *);

size_t mm_movie_sound_schedule_sound_count(const MMMovieSoundSchedule *, size_t scene);
MMScheduledSound mm_movie_sound_schedule_sound(const MMMovieSoundSchedule *, size_t scene, size_t index);
/* Borrow the schedule and publish a new owning sheet on success. Cues stay
 * in schedule order. Tracks sort by scene/actor, then signed actor ID and style. */
int mm_movie_sound_schedule_sheet(const MMMovieSoundSchedule *, size_t scene, MMMovieSoundSheet *);
void mm_movie_sound_sheet_dispose(MMMovieSoundSheet *);
/* The output array has room for sound_count entries. */
int mm_movie_sound_schedule_resume(const MMMovieSoundSchedule *, size_t scene, double frame,
                                    MMScheduledResume *out, size_t *count);

#ifdef __cplusplus
}
#endif
#endif
