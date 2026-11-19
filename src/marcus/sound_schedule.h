#ifndef MARCUS_SOUND_SCHEDULE_H
#define MARCUS_SOUND_SCHEDULE_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusSoundSchedule MarcusSoundSchedule;

enum MarcusSoundEventKind
{
    MARCUS_SCHEDULE_CUE,
    MARCUS_SCHEDULE_SCROLL,
    MARCUS_SCHEDULE_PAUSE
};
typedef struct MarcusScheduledCue
{
    long actor, style;
    int actor_sound, loop, queue, motion;
    int kind;
    double seconds;
    unsigned long resolved_type, resolved_chunk;
    long resolved_source;
    size_t source_row;
} MarcusScheduledCue;
typedef struct MarcusSoundEvent
{
    long frame;
    enum MarcusSoundEventKind kind;
    union
    {
        MarcusScheduledCue cue;
        double scroll;
        struct
        {
            int kind;
            long milliseconds;
        } pause;
    } data;
} MarcusSoundEvent;
typedef struct MarcusScheduledSound
{
    size_t scene, source_row;
    double start, end;
} MarcusScheduledSound;
typedef struct MarcusScheduledResume
{
    size_t scene, source_row;
    int queue;
    double seconds;
} MarcusScheduledResume;

MarcusSoundSchedule *marcus_sound_schedule_create(double frame_seconds);
void marcus_sound_schedule_destroy(MarcusSoundSchedule *);
void marcus_sound_schedule_clear(MarcusSoundSchedule *);
void marcus_sound_schedule_rate(MarcusSoundSchedule *, double fps);
double marcus_sound_schedule_frame_seconds(const MarcusSoundSchedule *);

/* Events are borrowed for the call. Allocation failure leaves the prior schedule intact.
 * The transition duration belongs to the outgoing edge of this scene. */
int marcus_sound_schedule_append(MarcusSoundSchedule *, long first, long last, double transition,
                                 const MarcusSoundEvent *, size_t count);
double marcus_sound_schedule_time_at(const MarcusSoundSchedule *, size_t scene, double frame);
double marcus_sound_schedule_frame_at(const MarcusSoundSchedule *, size_t scene, double time);
size_t marcus_sound_schedule_sound_count(const MarcusSoundSchedule *, size_t scene);
MarcusScheduledSound marcus_sound_schedule_sound(const MarcusSoundSchedule *, size_t scene, size_t index);
/* The output array has room for sound_count entries, an upper bound on resumed voices. */
int marcus_sound_schedule_resume(const MarcusSoundSchedule *, size_t scene, long frame,
                                 MarcusScheduledResume *out, size_t *count);

#ifdef __cplusplus
}
#endif
#endif
