#include "sound_sheet.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MarcusSoundSheetTrackName
{
    char name[sizeof(((MarcusSoundTrack *)0)->name)];
} MarcusSoundSheetTrackName;

typedef struct MarcusSoundSheetCueNames
{
    char name[sizeof(((MarcusSound *)0)->name)];
    char owner[sizeof(((MarcusSound *)0)->owner)];
} MarcusSoundSheetCueNames;

static int append_label(char *label, size_t capacity, const char *text)
{
    size_t offset = strlen(label), length = strlen(text), count = length;
    if (count >= capacity - offset)
    {
        count = capacity - offset - 1;
        while (count && ((unsigned char)text[count] & 0xc0) == 0x80)
            --count;
    }
    memcpy(label + offset, text, count);
    label[offset + count] = 0;
    return count == length;
}

static const char *owner_name(int actor_sound, MMI32 actor, char *fallback, const MarcusSoundSheetNames *names)
{
    const char *name;
    if (!actor_sound)
        return "Scene";
    name = names->actor(names->context, actor);
    if (name)
        return name;
    sprintf(fallback, "Actor %ld", (long)actor);
    return fallback;
}

static void label_track(MarcusSoundSheetTrackName *label, const MMMovieSoundTrack *track, const MarcusSoundSheetNames *names)
{
    char fallback[32];
    const char *kind = track->style == MARCUS_SOUND_MUSIC ? "Music" : track->style == MARCUS_SOUND_SPEECH ? "Speech" :
                       track->style == MARCUS_SOUND_EFFECT ? "Effects" : "Sound";
    const char *owner = owner_name(track->actor_sound, track->actor, fallback, names);
    if (append_label(label->name, sizeof(label->name), owner) &&
        append_label(label->name, sizeof(label->name), " / "))
        append_label(label->name, sizeof(label->name), kind);
}

static int label_cue(MarcusSoundSheetCueNames *label, const MMMovieTimingCue *cue, const MarcusSoundSheetNames *names)
{
    char fallback[32];
    const char *name = "Stop / silence";
    if (!(cue->flags & MM_TIMING_SILENT))
    {
        name = NULL;
        if (!names->sound(names->context, cue, &name))
            return 0;
        if (!name)
        {
            sprintf(fallback, "Sound %lu", (unsigned long)cue->sound_number);
            name = fallback;
        }
    }
    append_label(label->name, sizeof(label->name), name);
    name = owner_name((cue->flags & MM_TIMING_ACTOR_SOUND) != 0, cue->actor, fallback, names);
    append_label(label->owner, sizeof(label->owner), name);
    return 1;
}

void marcus_sound_sheet_clear(MarcusSoundSheet *sheet)
{
    mm_movie_sound_sheet_dispose(&sheet->model);
    free(sheet->tracks);
    free(sheet->cues);
    sheet->tracks = NULL;
    sheet->cues = NULL;
}

static void *labels(size_t count, size_t width)
{
    return count && count <= (size_t)-1 / width ? calloc(count, width) : NULL;
}

int marcus_sound_sheet_build(MarcusSoundSheet *out, const MMMovieSoundSchedule *schedule, size_t scene,
                             const MarcusSoundSheetNames *names)
{
    MarcusSoundSheet next = {0};
    size_t i;
    if (scene >= mm_movie_sound_schedule_size(schedule))
        return MARCUS_SOUND_SHEET_FAILED;
    if (!mm_movie_sound_schedule_sheet(schedule, scene, &next.model))
        return MARCUS_SOUND_SHEET_MEMORY;
    next.tracks = (MarcusSoundSheetTrackName *)labels(next.model.track_count, sizeof(*next.tracks));
    next.cues = (MarcusSoundSheetCueNames *)labels(next.model.cue_count, sizeof(*next.cues));
    if ((next.model.track_count && !next.tracks) || (next.model.cue_count && !next.cues))
    {
        marcus_sound_sheet_clear(&next);
        return MARCUS_SOUND_SHEET_MEMORY;
    }
    for (i = 0; i < next.model.track_count; ++i)
        label_track(next.tracks + i, next.model.tracks + i, names);
    for (i = 0; i < next.model.cue_count; ++i)
        if (!label_cue(next.cues + i, &next.model.cues[i].source, names))
        {
            marcus_sound_sheet_clear(&next);
            return MARCUS_SOUND_SHEET_FAILED;
        }
    marcus_sound_sheet_clear(out);
    *out = next;
    return MARCUS_SOUND_SHEET_OK;
}

const MMMovieSoundSheetCue *marcus_sound_sheet_cue(const MarcusSoundSheet *sheet, int index)
{
    return index >= 0 && (size_t)index < sheet->model.cue_count ? sheet->model.cues + index : NULL;
}

int marcus_sound_sheet_sound(const MarcusSoundSheet *sheet, int index, MarcusSound *out)
{
    const MMMovieSoundSheetCue *extent = marcus_sound_sheet_cue(sheet, index);
    const MMMovieTimingCue *cue;
    if (!extent || !out)
        return 0;
    cue = &extent->source;
    memset(out, 0, sizeof(*out));
    memcpy(out->name, sheet->cues[index].name, sizeof(out->name));
    memcpy(out->owner, sheet->cues[index].owner, sizeof(out->owner));
    out->track = (int)extent->track;
    out->carried = extent->carried;
    out->origin_scene = (int)extent->origin_scene;
    out->origin_frame = extent->origin_frame;
    out->frame = extent->frame;
    out->timeline_frame = extent->timeline_frame;
    out->end_frame = extent->end_frame;
    out->actor_id = cue->actor;
    out->source_id = cue->source;
    out->style = cue->style;
    out->volume = cue->volume;
    out->motion_cel = cue->motion_cel;
    out->chunk = cue->sound_number;
    out->child = cue->child;
    out->actor_sound = (cue->flags & MM_TIMING_ACTOR_SOUND) != 0;
    out->loop = (cue->flags & MM_TIMING_LOOP) != 0;
    out->queue = (cue->flags & MM_TIMING_QUEUE) != 0;
    out->silent = (cue->flags & MM_TIMING_SILENT) != 0;
    out->motion = (cue->flags & MM_TIMING_MOTION) != 0;
    return 1;
}

int marcus_sound_sheet_track(const MarcusSoundSheet *sheet, int index, MarcusSoundTrack *out)
{
    const MMMovieSoundTrack *track;
    if (!out || index < 0 || (size_t)index >= sheet->model.track_count)
        return 0;
    track = sheet->model.tracks + index;
    memset(out, 0, sizeof(*out));
    memcpy(out->name, sheet->tracks[index].name, sizeof(out->name));
    out->actor_sound = track->actor_sound;
    out->actor_id = track->actor;
    out->style = track->style;
    out->cue_count = (int)track->cue_count;
    return 1;
}

int marcus_sound_sheet_last(const MarcusSoundSheet *sheet, long frame, int style)
{
    size_t row = sheet->model.cue_count;
    while (row)
    {
        const MMMovieSoundSheetCue *cue = sheet->model.cues + --row;
        if (!(cue->source.flags & MM_TIMING_ACTOR_SOUND) && cue->frame == frame && cue->source.style == style)
            return row <= INT_MAX ? (int)row : -1;
    }
    return -1;
}
