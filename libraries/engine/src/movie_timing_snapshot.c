#include "mm/movie_timing_snapshot.h"
#include "mm/playback.h"
#include "mm/wide.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

enum
{
    HEADER_BYTES = 16, SCENE_BYTES = 32, SCROLL_BYTES = 12, PAUSE_BYTES = 12,
    CUE_WORDS = 16, CUE_BYTES = CUE_WORDS * 4 + 8
};
#define RECORD_LIMIT 1000000UL

static MMU32 read_word(const unsigned char *bytes)
{
    return (MMU32)bytes[0] | ((MMU32)bytes[1] << 8) |
        ((MMU32)bytes[2] << 16) | ((MMU32)bytes[3] << 24);
}

static void write_word(unsigned char *bytes, MMU32 word)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        bytes[i] = (unsigned char)(word >> (8 * i));
}

static int seconds_order(void)
{
    static const double one = 1.0;
    static const unsigned char little[8] = {0, 0, 0, 0, 0, 0, 240, 63};
    static const unsigned char big[8] = {63, 240, 0, 0, 0, 0, 0, 0};
    if (FLT_RADIX != 2 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 || sizeof(one) != sizeof(little))
        return 0;
    return !memcmp(&one, little, 8) ? 1 : !memcmp(&one, big, 8) ? -1 : 0;
}

static void seconds_bytes(unsigned char *out, const unsigned char *in, int order)
{
    unsigned i;
    for (i = 0; i < 8; ++i)
        out[i] = in[order > 0 ? i : 7 - i];
}

static int valid_seconds(double value)
{
    return value >= 0.0 && value <= DBL_MAX;
}

static int read_seconds(const unsigned char *bytes, int order, double *out)
{
    if ((bytes[7] & 0x7f) == 0x7f && (bytes[6] & 0xf0) == 0xf0)
        return 0;
    seconds_bytes((unsigned char *)out, bytes, order);
    return valid_seconds(*out);
}

static void write_seconds(unsigned char *bytes, int order, double value)
{
    seconds_bytes(bytes, (const unsigned char *)&value, order);
}

static int take_count(size_t count, size_t *remaining)
{
    if (count > *remaining)
        return 0;
    *remaining -= count;
    return 1;
}

static int pause_valid(const MMMovieTimingPause *pause)
{
    return pause->kind >= MM_PLAYBACK_WAIT_NONE && pause->kind <= MM_PLAYBACK_WAIT_TIME &&
        pause->milliseconds >= 0;
}

static int cue_valid(const MMMovieTimingCue *cue)
{
    const MMU32 flags = MM_TIMING_ACTOR_SOUND | MM_TIMING_LOOP | MM_TIMING_QUEUE | MM_TIMING_SILENT | MM_TIMING_MOTION;
    return !(cue->flags & ~flags) && valid_seconds(cue->seconds);
}

static void write_cue(unsigned char bytes[CUE_BYTES], const MMMovieTimingCue *cue, int order)
{
    write_word(bytes, (MMU32)cue->frame);
    write_word(bytes + 4, (MMU32)cue->source);
    write_word(bytes + 8, (MMU32)cue->actor);
    write_word(bytes + 12, (MMU32)cue->style);
    write_word(bytes + 16, (MMU32)cue->volume);
    write_word(bytes + 20, cue->sound_type);
    write_word(bytes + 24, cue->sound_number);
    write_word(bytes + 28, cue->scene_type);
    write_word(bytes + 32, cue->scene_number);
    write_word(bytes + 36, cue->child);
    write_word(bytes + 40, cue->flags);
    write_word(bytes + 44, (MMU32)cue->motion_cel);
    write_word(bytes + 48, (MMU32)cue->audio_kind);
    write_word(bytes + 52, cue->resolved_type);
    write_word(bytes + 56, cue->resolved_chunk);
    write_word(bytes + 60, (MMU32)cue->resolved_source);
    write_seconds(bytes + CUE_WORDS * 4, order, cue->seconds);
}

static int read_cue(const unsigned char bytes[CUE_BYTES], MMMovieTimingCue *cue, int order)
{
    cue->frame = mm_signed_word(read_word(bytes));
    cue->source = mm_signed_word(read_word(bytes + 4));
    cue->actor = mm_signed_word(read_word(bytes + 8));
    cue->style = mm_signed_word(read_word(bytes + 12));
    cue->volume = mm_signed_word(read_word(bytes + 16));
    cue->sound_type = read_word(bytes + 20);
    cue->sound_number = read_word(bytes + 24);
    cue->scene_type = read_word(bytes + 28);
    cue->scene_number = read_word(bytes + 32);
    cue->child = read_word(bytes + 36);
    cue->flags = read_word(bytes + 40);
    cue->motion_cel = mm_signed_word(read_word(bytes + 44));
    cue->audio_kind = mm_signed_word(read_word(bytes + 48));
    cue->resolved_type = read_word(bytes + 52);
    cue->resolved_chunk = read_word(bytes + 56);
    cue->resolved_source = mm_signed_word(read_word(bytes + 60));
    return read_seconds(bytes + CUE_WORDS * 4, order, &cue->seconds) && cue_valid(cue);
}

static int write_scene(const MMMovieTimingWriter *writer, const MMMovieTimingScene *scene, int order)
{
    unsigned char bytes[CUE_BYTES];
    size_t i;
    write_word(bytes, scene->range.chunk);
    write_word(bytes + 4, (MMU32)scene->range.first);
    write_word(bytes + 8, (MMU32)scene->range.last);
    write_seconds(bytes + 12, order, scene->transition);
    write_word(bytes + 20, (MMU32)scene->scroll_count);
    write_word(bytes + 24, (MMU32)scene->pause_count);
    write_word(bytes + 28, (MMU32)scene->cue_count);
    if (!writer->write(writer->context, bytes, SCENE_BYTES))
        return 0;
    for (i = 0; i < scene->scroll_count; ++i)
    {
        if ((i && scene->scrolls[i - 1].frame >= scene->scrolls[i].frame) ||
            !valid_seconds(scene->scrolls[i].seconds))
            return 0;
        write_word(bytes, (MMU32)scene->scrolls[i].frame);
        write_seconds(bytes + 4, order, scene->scrolls[i].seconds);
        if (!writer->write(writer->context, bytes, SCROLL_BYTES))
            return 0;
    }
    for (i = 0; i < scene->pause_count; ++i)
    {
        if (!pause_valid(&scene->pauses[i]))
            return 0;
        write_word(bytes, (MMU32)scene->pauses[i].frame);
        write_word(bytes + 4, (MMU32)scene->pauses[i].kind);
        write_word(bytes + 8, (MMU32)scene->pauses[i].milliseconds);
        if (!writer->write(writer->context, bytes, PAUSE_BYTES))
            return 0;
    }
    for (i = 0; i < scene->cue_count; ++i)
    {
        if (!cue_valid(&scene->cues[i]))
            return 0;
        write_cue(bytes, &scene->cues[i], order);
        if (!writer->write(writer->context, bytes, CUE_BYTES))
            return 0;
    }
    return 1;
}

int mm_movie_timing_snapshot_write(const MMMovieTimingWriter *writer, size_t count, double frame_seconds)
{
    unsigned char bytes[HEADER_BYTES];
    MMMovieTimingScene scene;
    size_t i, remaining = RECORD_LIMIT;
    int order = seconds_order();
    if (!order || !valid_seconds(frame_seconds) || frame_seconds == 0 || count > 0xffffffffUL)
        return 0;
    memcpy(bytes, "MMT4", 4);
    write_seconds(bytes + 4, order, frame_seconds);
    write_word(bytes + 12, (MMU32)count);
    if (!writer->write(writer->context, bytes, sizeof(bytes)))
        return 0;
    for (i = 0; i < count; ++i)
        if (!writer->scene(writer->context, i, &scene) || !valid_seconds(scene.transition) ||
            !take_count(scene.scroll_count, &remaining) || !take_count(scene.pause_count, &remaining) ||
            !take_count(scene.cue_count, &remaining) || !write_scene(writer, &scene, order))
            return 0;
    return 1;
}

static int read_counts(const unsigned char *bytes, MMMovieTimingScene *scene, size_t *remaining)
{
    MMU32 scrolls = read_word(bytes), pauses = read_word(bytes + 4), cues = read_word(bytes + 8);
    if ((size_t)scrolls != scrolls || (size_t)pauses != pauses || (size_t)cues != cues ||
        !take_count((size_t)scrolls, remaining) || !take_count((size_t)pauses, remaining) ||
        !take_count((size_t)cues, remaining))
        return 0;
    scene->scroll_count = (size_t)scrolls;
    scene->pause_count = (size_t)pauses;
    scene->cue_count = (size_t)cues;
    return 1;
}

static int read_records(const MMMovieTimingReader *reader, const MMMovieTimingScene *scene, int order,
                        MMMovieTimingScroll *scrolls, MMMovieTimingPause *pauses, MMMovieTimingCue *cues)
{
    unsigned char bytes[CUE_BYTES];
    size_t i;
    for (i = 0; i < scene->scroll_count; ++i)
    {
        if (!reader->read(reader->context, bytes, SCROLL_BYTES) || !read_seconds(bytes + 4, order, &scrolls[i].seconds))
            return 0;
        scrolls[i].frame = mm_signed_word(read_word(bytes));
        if (i && scrolls[i - 1].frame >= scrolls[i].frame)
            return 0;
    }
    for (i = 0; i < scene->pause_count; ++i)
    {
        if (!reader->read(reader->context, bytes, PAUSE_BYTES))
            return 0;
        pauses[i].frame = mm_signed_word(read_word(bytes));
        pauses[i].kind = mm_signed_word(read_word(bytes + 4));
        pauses[i].milliseconds = mm_signed_word(read_word(bytes + 8));
        if (!pause_valid(&pauses[i]))
            return 0;
    }
    for (i = 0; i < scene->cue_count; ++i)
        if (!reader->read(reader->context, bytes, CUE_BYTES) || !read_cue(bytes, &cues[i], order))
            return 0;
    return 1;
}

static int read_scene(const MMMovieTimingReader *reader, size_t index, int order, size_t *remaining)
{
    unsigned char bytes[SCENE_BYTES];
    MMMovieTimingRange expected;
    MMMovieTimingScene scene;
    MMMovieTimingScroll *scrolls = NULL;
    MMMovieTimingPause *pauses = NULL;
    MMMovieTimingCue *cues = NULL;
    int ok;
    if (!reader->read(reader->context, bytes, sizeof(bytes)) ||
        !reader->expected(reader->context, index, &expected))
        return 0;
    scene.range.chunk = read_word(bytes);
    scene.range.first = mm_signed_word(read_word(bytes + 4));
    scene.range.last = mm_signed_word(read_word(bytes + 8));
    if (scene.range.chunk != expected.chunk || scene.range.first != expected.first || scene.range.last != expected.last ||
        !read_seconds(bytes + 12, order, &scene.transition) || !read_counts(bytes + 20, &scene, remaining))
        return 0;
    if (scene.scroll_count)
        scrolls = (MMMovieTimingScroll *)calloc(scene.scroll_count, sizeof(*scrolls));
    if (scene.pause_count)
        pauses = (MMMovieTimingPause *)calloc(scene.pause_count, sizeof(*pauses));
    if (scene.cue_count)
        cues = (MMMovieTimingCue *)calloc(scene.cue_count, sizeof(*cues));
    scene.scrolls = scrolls;
    scene.pauses = pauses;
    scene.cues = cues;
    ok = (!scene.scroll_count || scrolls) && (!scene.pause_count || pauses) && (!scene.cue_count || cues) &&
        read_records(reader, &scene, order, scrolls, pauses, cues) && reader->scene(reader->context, index, &scene);
    free(cues);
    free(pauses);
    free(scrolls);
    return ok;
}

int mm_movie_timing_snapshot_read(const MMMovieTimingReader *reader, size_t expected_count, double *frame_seconds)
{
    unsigned char bytes[HEADER_BYTES];
    double seconds;
    size_t i, remaining = RECORD_LIMIT;
    int order = seconds_order();
    if (!order || !reader->read(reader->context, bytes, sizeof(bytes)) || memcmp(bytes, "MMT4", 4) ||
        !read_seconds(bytes + 4, order, &seconds) || seconds == 0 || read_word(bytes + 12) != expected_count)
        return 0;
    for (i = 0; i < expected_count; ++i)
        if (!read_scene(reader, i, order, &remaining))
            return 0;
    if (!reader->at_end(reader->context))
        return 0;
    *frame_seconds = seconds;
    return 1;
}
