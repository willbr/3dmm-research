#include "player.h"
#include "camera_guide.h"
#include "live_document.h"
#include "sound_sheet.h"
#include "caption_runs.h"
#include "caption_raster.h"
#include "caption_faces.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            fprintf(stderr, "line %d: %s: %s\n", __LINE__, #value, marcus_error(player));                              \
            goto failed;                                                                                               \
        }                                                                                                              \
    } while (0)

static int orbit_centred(const MarcusProjection *projection, const MarcusBounds *bounds)
{
    double center[3], offset;
    int axis, corner, row;
    for (axis = 0; axis < 3; ++axis)
    {
        center[axis] = 0;
        for (corner = 0; corner < 8; ++corner)
            center[axis] += bounds->corners[corner * 3 + axis] / 8.0;
    }
    for (row = 0; row < 2; ++row)
    {
        offset = 0;
        for (axis = 0; axis < 3; ++axis)
            offset += (center[axis] - projection->transform[9 + axis]) * projection->transform[row * 3 + axis];
        if (fabs(offset) > 0.1)
            return 0;
    }
    return bounds->valid;
}
/* Camera math only: projection used by world-space controls must agree with
 * the renderer for rotated cameras and both perspective/orthographic lenses. */
static int matrices_match(const MarcusProjection *projection)
{
    float view[16], clip[16];
    double local[4], world[4], camera[4], screen[4], depth, span;
    int sample, row, axis;
    if (!camera_projection_matrices(projection, view, clip)) return 0;
    for (sample = 0; sample < 3; ++sample)
    {
        depth = sample == 0 ? projection->near_clip : sample == 1 ? projection->far_clip :
            (projection->near_clip + projection->far_clip) / 2;
        span = projection->orthographic ? projection->ortho_height / 2 :
            depth * tan(projection->vertical_fov * .008726646259971648);
        local[0] = span * projection->aspect / 2;
        local[1] = span / 4;
        local[2] = -depth;
        local[3] = world[3] = 1;
        for (axis = 0; axis < 3; ++axis)
        {
            world[axis] = projection->transform[9 + axis];
            for (row = 0; row < 3; ++row) world[axis] += projection->transform[row * 3 + axis] * local[row];
        }
        for (row = 0; row < 4; ++row)
        {
            camera[row] = 0;
            for (axis = 0; axis < 4; ++axis) camera[row] += view[axis * 4 + row] * world[axis];
            if (fabs(camera[row] - local[row]) > .002 * (1 + fabs(local[row]))) return 0;
        }
        for (row = 0; row < 4; ++row)
        {
            screen[row] = 0;
            for (axis = 0; axis < 4; ++axis) screen[row] += clip[axis * 4 + row] * camera[axis];
        }
        if (fabs(screen[0] / screen[3] - .5) > .002 || fabs(screen[1] / screen[3] - .25) > .002) return 0;
        if (sample < 2 && fabs(screen[2] / screen[3] - (sample ? 1 : -1)) > .002) return 0;
    }
    return 1;
}
typedef struct LiveDocumentProbe
{
    char effects[24];
    size_t count;
    MMI32 reject_source;
    int reject_allocations;
} LiveDocumentProbe;

static int live_effect(void *context, char effect)
{
    LiveDocumentProbe *probe = (LiveDocumentProbe *)context;
    if (probe->count >= sizeof(probe->effects) - 1) return 0;
    probe->effects[probe->count++] = effect;
    probe->effects[probe->count] = 0;
    return 1;
}

static int live_resources(void *p) { return live_effect(p, 'R'); }
static int live_retail(void *p) { return live_effect(p, 'L'); }
static int live_mount(void *p, MMI32 source, const void *file)
{
    const LiveDocumentProbe *probe = (const LiveDocumentProbe *)p;
    return file && source >= 30 && source <= 33 && live_effect(p, (char)('0' + source - 30)) &&
           source != probe->reject_source;
}
static int live_install(void *p, const MMPreviewLiveDefinitions *definitions)
{
    MMPreviewLiveScene scene;
    definitions->scene(definitions->context, 0, &scene);
    return definitions->scene_count == 2 && definitions->prop_count == 2 && scene.number == 7 && live_effect(p, 'I');
}
static int live_reset_names(void *p) { return live_effect(p, 'N'); }
static int live_name(void *p, MMI32 actor, const char *name)
{
    return ((actor == 9 && !strcmp(name, "Retail")) || (actor == 10 && !strcmp(name, "Generated"))) &&
           live_effect(p, 'P');
}
static int live_texts(void *p, const MMMovieTextJournal *texts)
{
    return texts->count == 1 && live_effect(p, 'T');
}
static void live_hold(void *p) { ++*(int *)p; }
static void live_release(void *p) { --*(int *)p; }
static void *live_resize(void *p, void *previous, MMU32 old_bytes, MMU32 count, int priority)
{
    (void)old_bytes;
    (void)priority;
    return ((LiveDocumentProbe *)p)->reject_allocations ? NULL : realloc(previous, count);
}
static void live_dispose(void *p, void *block, MMU32 count)
{
    (void)p;
    (void)count;
    free(block);
}

/* Scene metadata and archive order only, with no renderer or UI input. */
static int check_live_document(MarcusPlayer *player)
{
    static const unsigned char paint[MARCUS_BOARD_PIXELS] = {0};
    const MMMovieEditFileServices files = {live_hold, live_release};
    const MarcusScene authored = {"Authored", 40, -4, 80, MARCUS_TRANSITION_DISSOLVE};
    const MMMovieSceneRange range = {40, -6, 83}, inserted_range = {7, 1, 5};
    MMPreviewLiveScene definition = {7, 1, 2};
    MMPreviewLiveProp prop = {7, 41, 42, 2, 9, 1, 5, {0, 0, 0}};
    MMMovieActorTextEdit text = {0};
    MMMovieActorTrackEdit track = {7, 41, 33, NULL};
    MarcusLiveDocument doc;
    MMMovieSceneDefinitions definitions = {0};
    MMMovieRangeJournal ranges = {0};
    MMMoviePropJournal props = {0};
    MMMovieTextJournal texts = {0};
    MMMovieActorTrackJournal tracks = {0};
    MarcusBoardStore boards = {0};
    MarcusBoardWire board = {0};
    MarcusSceneRows base = {0}, current = {0};
    MarcusScene *rows;
    LiveDocumentProbe probe = {{0}, 0, 0, 0};
    MMStorageAllocator allocator = {NULL, live_resize, live_dispose};
    MarcusLiveDocumentServices s = {NULL, live_resources, live_retail, live_mount, live_install,
                                    live_reset_names, live_name, live_texts};
    int file = 1, ok = 0;
    allocator.context = s.context = &probe;
    CHECK(marcus_scene_rows_init(&base, NULL) && marcus_scene_rows_init(&current, &allocator));
    CHECK(mm_movie_scene_definitions_init(&definitions, NULL) && mm_movie_range_journal_init(&ranges, NULL) &&
          mm_movie_prop_journal_init(&props, NULL, &files) && mm_movie_text_journal_init(&texts, NULL, &files) &&
          mm_movie_track_journal_init(&tracks, NULL, &files) && marcus_board_store_init(&boards, NULL, &files));
    CHECK(marcus_scene_rows_resize(&base, 1) && marcus_scene_rows_resize(&current, 1));
    *(MarcusScene *)base.storage.buffers[0].data = authored;
    *(MarcusScene *)current.storage.buffers[0].data = authored;
    CHECK(mm_movie_scene_definitions_append(&definitions, &definition));
    definition.number = 3;
    definition.source = 30;
    CHECK(mm_movie_scene_definitions_append(&definitions, &definition));
    CHECK(mm_movie_range_journal_set(&ranges, &range) && mm_movie_range_journal_set(&ranges, &inserted_range));
    CHECK(mm_movie_prop_journal_append(&props, &prop, "Retail", NULL));
    prop.number = 42;
    prop.source = 31;
    prop.actor_id = 10;
    CHECK(mm_movie_prop_journal_append(&props, &prop, "Generated", &file));
    text.scene = 7;
    text.actor = 42;
    text.source = 32;
    text.file = track.file = &file;
    CHECK(mm_movie_text_journal_set(&texts, &text) && mm_movie_track_journal_set(&tracks, &track));
    board.values.angle_count = 1;
    board.values.colour_count = MARCUS_BOARD_SWATCH_COUNT;
    memcpy(board.values.colours, marcus_board_swatches, sizeof(marcus_board_swatches));
    board.values.angles[0].fov = 45;
    board.values.angles[0].near_clip = 1;
    board.values.angles[0].far_clip = 100;
    board.pixels[0] = paint;
    CHECK(marcus_board_store_set(&boards, 3, 30, 1, &file, &board));
    CHECK(marcus_board_store_set(&boards, 7, 0, 1, &file, &board));
    doc.retail_source = 2;
    doc.definitions.scenes = &definitions;
    doc.definitions.props = &props;
    doc.boards = &boards;
    doc.texts = &texts;
    doc.tracks = &tracks;
    doc.ranges = &ranges;

    CHECK(marcus_live_document_mount_boards(&boards, &s) && !strcmp(probe.effects, "0"));
    probe.count = 0;
    probe.reject_source = 33;
    CHECK(marcus_live_document_apply(&doc, &s, &current, &base) == MARCUS_LIVE_DOCUMENT_FAILED);
    CHECK(!strcmp(probe.effects, "RL0123") && current.count == 1);
    probe.count = 0;
    probe.reject_source = 0;
    probe.reject_allocations = 1;
    CHECK(marcus_live_document_apply(&doc, &s, &current, &base) == MARCUS_LIVE_DOCUMENT_MEMORY);
    rows = (MarcusScene *)current.storage.buffers[0].data;
    CHECK(current.count == 1 && rows[0].first_frame == -4 && !strcmp(rows[0].name, "Authored"));
    probe.count = 0;
    probe.reject_allocations = 0;
    CHECK(marcus_live_document_apply(&doc, &s, &current, &base) == MARCUS_LIVE_DOCUMENT_OK);
    CHECK(!strcmp(probe.effects, "RL0123INPPT") && current.count == 3);
    rows = (MarcusScene *)current.storage.buffers[0].data;
    CHECK(rows[0].chunk == 40 && rows[0].first_frame == -6 && rows[0].last_frame == 83 &&
          rows[0].transition == MARCUS_TRANSITION_DISSOLVE && !strcmp(rows[0].name, "Authored"));
    CHECK(rows[1].chunk == 7 && rows[1].last_frame == 5 && !strcmp(rows[1].name, "Scene 2"));
    CHECK(rows[2].chunk == 3 && rows[2].first_frame == 1 && rows[2].last_frame == 1 && !strcmp(rows[2].name, "Scene 3"));
    ok = 1;
failed:
    marcus_scene_rows_dispose(&current);
    marcus_scene_rows_dispose(&base);
    marcus_board_store_dispose(&boards);
    mm_movie_track_journal_dispose(&tracks);
    mm_movie_text_journal_dispose(&texts);
    mm_movie_prop_journal_dispose(&props);
    mm_movie_range_journal_dispose(&ranges);
    mm_movie_scene_definitions_dispose(&definitions);
    return ok && file == 1;
}

typedef struct SoundSheetProbe
{
    char actor[194];
    int reads, reject;
} SoundSheetProbe;

static const char *sheet_actor(void *context, MMI32 actor)
{
    return actor == 4 ? ((SoundSheetProbe *)context)->actor : NULL;
}

static int sheet_sound(void *context, const MMMovieTimingCue *cue, const char **name)
{
    SoundSheetProbe *probe = (SoundSheetProbe *)context;
    ++probe->reads;
    *name = cue->sound_number == 7 ? "Authored sound" : NULL;
    return !probe->reject;
}

static int check_sound_sheet(MarcusPlayer *player)
{
    MMMovieSoundSchedule *schedule = mm_movie_sound_schedule_create(.125);
    MMMovieTimingCue cues[3] = {{0}};
    MMMovieTimingScene scene = {0};
    MarcusSoundSheet sheet = {0};
    SoundSheetProbe probe = {{0}, 0, 0};
    MarcusSoundSheetNames names = {NULL, sheet_actor, sheet_sound};
    MarcusSoundTrack track;
    MarcusSound sound;
    int ok = 0;
    CHECK(schedule);
    names.context = &probe;
    memset(probe.actor, 'A', 190);
    memcpy(probe.actor + 190, "\xe2\x82\xac", 3);
    scene.range.first = -2;
    scene.range.last = 4;
    scene.cues = cues;
    scene.cue_count = 3;
    cues[0].frame = -1;
    cues[0].actor = 4;
    cues[0].style = MARCUS_SOUND_EFFECT;
    cues[0].flags = MM_TIMING_ACTOR_SOUND | MM_TIMING_MOTION;
    cues[0].motion_cel = 2;
    cues[0].sound_number = 7;
    cues[0].source = 32;
    cues[1].frame = -1;
    cues[1].style = MARCUS_SOUND_EFFECT;
    cues[1].sound_number = 8;
    cues[2].frame = 0;
    cues[2].style = MARCUS_SOUND_EFFECT;
    cues[2].flags = MM_TIMING_SILENT;
    CHECK(mm_movie_sound_schedule_append(schedule, &scene));
    CHECK(marcus_sound_sheet_build(&sheet, schedule, 0, &names) == MARCUS_SOUND_SHEET_OK);
    CHECK(probe.reads == 2 && sheet.model.cue_count == 3 && sheet.model.track_count == 2);
    CHECK(marcus_sound_sheet_track(&sheet, 1, &track) && track.actor_sound &&
          track.actor_id == 4 && strlen(track.name) == 190);
    CHECK(marcus_sound_sheet_sound(&sheet, 0, &sound) && sound.motion && sound.motion_cel == 2 &&
          sound.actor_id == 4 && sound.source_id == 32 && strlen(sound.owner) == 190 &&
          !strcmp(sound.name, "Authored sound"));
    CHECK(marcus_sound_sheet_last(&sheet, -1, MARCUS_SOUND_EFFECT) == 1);
    CHECK(marcus_sound_sheet_sound(&sheet, 1, &sound) && !strcmp(sound.name, "Sound 8") &&
          !strcmp(sound.owner, "Scene"));
    CHECK(marcus_sound_sheet_sound(&sheet, 2, &sound) && sound.silent && !strcmp(sound.name, "Stop / silence"));
    probe.reject = 1;
    CHECK(marcus_sound_sheet_build(&sheet, schedule, 0, &names) == MARCUS_SOUND_SHEET_FAILED);
    mm_movie_sound_schedule_destroy(schedule);
    schedule = NULL;
    CHECK(marcus_sound_sheet_cue(&sheet, 0)->source.sound_number == 7);
    CHECK(marcus_sound_sheet_sound(&sheet, 0, &sound) && !strcmp(sound.name, "Authored sound"));
    ok = 1;
failed:
    marcus_sound_sheet_clear(&sheet);
    mm_movie_sound_schedule_destroy(schedule);
    return ok;
}

typedef struct CaptionRunProbe
{
    unsigned char coverage;
    int calls, available, released, fail_at;
} CaptionRunProbe;

static int caption_run_face(void *p, const MMCaptionStyle *style)
{
    (void)p;
    return style->pixels == 12;
}

static int caption_run_measure(void *p, const unsigned short *text, MMI32 count, MarcusCaptionMetrics *out)
{
    (void)p;
    (void)text;
    out->advance = count * 6;
    out->ascent = 9;
    out->cell_height = 12;
    out->leading = 1;
    return 1;
}

static void caption_run_release(void *p)
{
    ++((CaptionRunProbe *)p)->released;
}

static int caption_run_mask(void *p, const unsigned short *text, MMI32 count, MarcusCaptionMaskOwner *out)
{
    CaptionRunProbe *probe = p;
    (void)text;
    (void)count;
    if (++probe->calls == probe->fail_at)
        return 0;
    ++probe->available;
    out->raster.geometry.width = out->raster.geometry.height = out->raster.geometry.pitch = 1;
    out->raster.ink = &probe->coverage;
    out->raster.density = 1.f;
    out->bytes = 1;
    out->owner = probe;
    out->release = caption_run_release;
    return 1;
}

static int check_caption_runs(MarcusPlayer *player)
{
    unsigned short text[] = {'A', '\r', '\n', 'B'};
    MarcusCaptionInput input = {{2, 4, 200, 36}, NULL, 4, 12, {"Test", 4}, 0};
    CaptionRunProbe probe = {255, 0, 0, 0, 0};
    MarcusCaptionFonts fonts = {NULL, NULL, caption_run_face, caption_run_measure, caption_run_mask};
    MarcusCaptionRuns first = {0}, second = {0};
    const MarcusCaptionRun *runs;
    const MarcusCaptionStop *stops;
    size_t scene_bytes = 0, first_bytes;
    int ok = 0;
    input.text = text;
    fonts.context = &probe;
    CHECK(marcus_caption_runs_layout(&first, &input, &fonts, &scene_bytes, 32, 1) == 0);
    CHECK(first.count == 2 && first.height > 12 && scene_bytes == 2 && first.stop_count > 2);
    runs = (const MarcusCaptionRun *)first.storage.buffers[0].data;
    stops = marcus_caption_runs_stops(&first);
    text[0] = 'Z';
    CHECK(runs[0].text[0] == 'A' && !strcmp(runs[0].style.font.name, "Test"));
    CHECK(stops[0].offset == 0 && stops[first.stop_count - 1].offset == 3);
    first_bytes = scene_bytes;
    probe.fail_at = probe.calls + 2;
    CHECK(marcus_caption_runs_layout(&second, &input, &fonts, &scene_bytes, 32, 0) == MARCUS_CAPTION_RUNS_RASTER);
    CHECK(!second.ready && scene_bytes == first_bytes && probe.released == 1);
    CHECK(marcus_caption_runs_layout(&second, &input, &fonts, &scene_bytes, first_bytes, 0) == MARCUS_CAPTION_RUNS_BUDGET);
    CHECK(!second.ready && scene_bytes == first_bytes && probe.released == 2);
    CHECK(marcus_caption_runs_layout(&second, &input, &fonts, &scene_bytes, 32, 0) == 0);
    CHECK(second.count == 2 && !second.stop_count && scene_bytes == 4);
    ok = 1;
failed:
    marcus_caption_runs_clear(&second);
    marcus_caption_runs_clear(&first);
    return ok && scene_bytes == 0 && probe.available == probe.released;
}

typedef struct CaptionRasterProbe
{
    MMI32 height, advance;
    int calls, released, fail;
} CaptionRasterProbe;

static int caption_raster_face(void *p, const MMCaptionStyle *style)
{
    (void)p;
    return style->pixels == 48;
}

static int caption_raster_measure(void *p, const unsigned short *text, MMI32 count, MarcusCaptionMetrics *out)
{
    const CaptionRasterProbe *probe = p;
    (void)text;
    (void)count;
    out->advance = probe->advance;
    out->ascent = 21;
    out->cell_height = probe->height;
    out->leading = 0;
    return 1;
}

static void caption_raster_release(void *p)
{
    ++((CaptionRasterProbe *)p)->released;
}

static int caption_raster_mask(void *p, const unsigned short *text, MMI32 count, MarcusCaptionMaskOwner *out)
{
    static const unsigned char coverage[] = {0, 2, 8, 10, 2, 4, 10, 12, 20, 22, 28, 30, 22, 24, 30, 32};
    CaptionRasterProbe *probe = p;
    (void)text;
    (void)count;
    ++probe->calls;
    if (probe->fail)
        return 0;
    out->raster.geometry.width = out->raster.geometry.height = out->raster.geometry.pitch = 4;
    out->raster.ink = coverage;
    out->bytes = sizeof(coverage);
    out->owner = p;
    out->release = caption_raster_release;
    return 1;
}

static int check_caption_rasters(MarcusPlayer *player)
{
    unsigned char ink[] = {10, 20, 99}, paper[] = {100, 150, 23};
    unsigned short text[] = {'A'};
    const unsigned char smoothed[] = {2, 10, 22, 30}, expanded[] = {10, 10, 20, 20, 10, 10, 20, 20};
    const MMCaptionMask native = {-1, -2, 2, 1, 3};
    CaptionRasterProbe probe = {26, 8, 0, 0, 0};
    MarcusCaptionFonts fonts = {NULL, NULL, caption_raster_face, caption_raster_measure, caption_raster_mask};
    MarcusCaptionRasterCache cache = {0};
    MarcusCaptionRun run = {0};
    MarcusCaptionRaster raster;
    const unsigned char *previous;
    int ok = 0;
    fonts.context = &probe;
    run.style.pixels = 12;
    run.style.font.name = "Probe";
    run.style.font.length = 5;
    run.text = text;
    run.length = 1;
    run.ascent = 9;
    run.height = 13;
    run.mask.raster.geometry = native;
    run.mask.raster.ink = ink;
    run.mask.raster.paper = paper;
    CHECK(marcus_caption_raster_get(&cache, &run, 2, 32768, &fonts, &raster) == MARCUS_CAPTION_RASTER_OK);
    CHECK(raster.density == 2.f && raster.ascent == 21 && raster.geometry.width == 2 && raster.geometry.height == 2);
    CHECK(!raster.paper && !memcmp(raster.ink, smoothed, sizeof(smoothed)) && probe.calls == 1 && probe.released == 1);
    text[0] = 'B';
    probe.height = 12;
    CHECK(marcus_caption_raster_get(&cache, &run, 2, 32768, &fonts, &raster) == MARCUS_CAPTION_RASTER_OK);
    CHECK(raster.density == 2.f && raster.ascent == 36 && raster.geometry.left == -2 && raster.geometry.top == -4);
    CHECK(!memcmp(raster.ink, expanded, sizeof(expanded)) && raster.paper[7] == 150 && probe.released == 2);
    text[0] = 'A';
    probe.fail = 1;
    CHECK(marcus_caption_raster_get(&cache, &run, 2, 32768, &fonts, &raster) == MARCUS_CAPTION_RASTER_OK);
    CHECK(!memcmp(raster.ink, smoothed, sizeof(smoothed)) && probe.calls == 2);
    previous = raster.ink;
    text[0] = 'C';
    CHECK(marcus_caption_raster_get(&cache, &run, 2, 32768, &fonts, &raster) == MARCUS_CAPTION_RASTER_SOURCE);
    CHECK(raster.ink == previous && cache.count == 2 && probe.released == 2);
    marcus_caption_raster_clear(&cache);
    CHECK(marcus_caption_raster_get(&cache, &run, 2, 8, &fonts, &raster) == MARCUS_CAPTION_RASTER_OK);
    CHECK(raster.density == 1.f && raster.ascent == 18 && raster.geometry.pitch == 3 && probe.calls == 3);
    ink[0] = 0;
    CHECK(raster.ink[0] == 10 && raster.ink[2] == 99);
    text[0] = 'D';
    CHECK(marcus_caption_raster_get(&cache, &run, 2, 8, &fonts, &raster) == MARCUS_CAPTION_RASTER_OK);
    CHECK(!raster.ink[0] && cache.count == 1 && cache.bytes == 6);
    ok = 1;
failed:
    marcus_caption_raster_clear(&cache);
    return ok;
}

typedef struct CaptionFaceProbe
{
    int bundled, installed, system, released;
    int bundle_result, system_result;
    int saw_arial, saw_unicode;
} CaptionFaceProbe;

static int caption_face_bundled(void *p, const MMCaptionFont *name, const MarcusCaptionFaceOptions *options, void **out)
{
    CaptionFaceProbe *probe = p;
    (void)options;
    ++probe->bundled;
    if (name->length == 5 && !memcmp(name->name, "Arial", 5))
        probe->saw_arial = 1;
    if (probe->bundle_result == MARCUS_CAPTION_FACE_READY)
        *out = probe;
    return probe->bundle_result;
}

static int caption_face_installed(void *p, const void *name, size_t units, int utf16,
                                  const MarcusCaptionFaceOptions *options, void **out)
{
    const unsigned short non_ascii[] = {'A', 0xe9, 0xd83d, 0xdcbe};
    CaptionFaceProbe *probe = p;
    if (utf16)
    {
        ++probe->installed;
        if (units == 4 && !memcmp(name, non_ascii, sizeof(non_ascii)) &&
            options->bold && options->italic && options->underline)
            probe->saw_unicode = 1;
        return MARCUS_CAPTION_FACE_MISSING;
    }
    if (units != 6 || memcmp(name, "System", 6))
        return MARCUS_CAPTION_FACE_FAILED;
    ++probe->system;
    if (probe->system_result == MARCUS_CAPTION_FACE_READY)
        *out = probe;
    return probe->system_result;
}

static void caption_face_release(void *p)
{
    ++((CaptionFaceProbe *)p)->released;
}

static int check_caption_faces(MarcusPlayer *player)
{
    const char unicode[] = "A\xc3\xa9\xf0\x9f\x92\xbe";
    CaptionFaceProbe probe = {0};
    MarcusCaptionFaces cache = {0};
    MarcusCaptionFaceServices services = {NULL, caption_face_bundled, caption_face_installed, caption_face_release};
    MMCaptionStyle style = {{NULL, 0}, 12, 7, 0, 0, 0};
    void *face = NULL;
    int ok = 0, i;
    services.context = &probe;

    probe.bundle_result = MARCUS_CAPTION_FACE_FAILED;
    CHECK(marcus_caption_face_get(&cache, &style, &services, &face) == MARCUS_CAPTION_FACE_FAILED);
    CHECK(probe.saw_arial && !probe.installed && !probe.system && !face && !cache.count);
    probe.bundle_result = MARCUS_CAPTION_FACE_READY;
    CHECK(marcus_caption_face_get(&cache, &style, &services, &face) == MARCUS_CAPTION_FACE_READY);
    CHECK(face == &probe && cache.count == 1 && probe.bundled == 2);
    probe.bundle_result = MARCUS_CAPTION_FACE_MISSING;
    CHECK(marcus_caption_face_get(&cache, &style, &services, &face) == MARCUS_CAPTION_FACE_READY);
    CHECK(probe.bundled == 2);

    style.font.name = unicode;
    style.font.length = sizeof(unicode) - 1;
    probe.system_result = MARCUS_CAPTION_FACE_READY;
    CHECK(marcus_caption_face_get(&cache, &style, &services, &face) == MARCUS_CAPTION_FACE_READY);
    CHECK(face == &probe && probe.saw_unicode && probe.installed == 1 && probe.system == 1);
    style.font.name = "Missing";
    style.font.length = 7;
    probe.system_result = MARCUS_CAPTION_FACE_MISSING;
    face = NULL;
    CHECK(marcus_caption_face_get(&cache, &style, &services, &face) == MARCUS_CAPTION_FACE_MISSING);
    CHECK(!face && cache.count == 3 && probe.installed == 2 && probe.system == 2);
    CHECK(marcus_caption_face_get(&cache, &style, &services, &face) == MARCUS_CAPTION_FACE_MISSING);
    CHECK(!face && probe.installed == 2 && probe.system == 2);
    marcus_caption_faces_clear(&cache);
    CHECK(probe.released == 2);

    probe.bundle_result = MARCUS_CAPTION_FACE_READY;
    for (i = 1; i <= MARCUS_CAPTION_FACE_LIMIT; ++i)
    {
        style.pixels = i;
        CHECK(marcus_caption_face_get(&cache, &style, &services, &face) == MARCUS_CAPTION_FACE_READY);
    }
    CHECK(cache.count == MARCUS_CAPTION_FACE_LIMIT);
    ++style.pixels;
    probe.bundle_result = MARCUS_CAPTION_FACE_FAILED;
    CHECK(marcus_caption_face_get(&cache, &style, &services, &face) == MARCUS_CAPTION_FACE_FAILED);
    CHECK(cache.count == MARCUS_CAPTION_FACE_LIMIT && probe.released == 2);
    probe.bundle_result = MARCUS_CAPTION_FACE_READY;
    CHECK(marcus_caption_face_get(&cache, &style, &services, &face) == MARCUS_CAPTION_FACE_READY);
    CHECK(cache.count == 1 && probe.released == 2 + MARCUS_CAPTION_FACE_LIMIT);
    ok = 1;
failed:
    marcus_caption_faces_clear(&cache);
    return ok && probe.released == 3 + MARCUS_CAPTION_FACE_LIMIT;
}

int main(int argc, char **argv)
{
    MarcusPlayer *player, *other;
    MarcusMovieInput *input;
    MarcusInfo info;
    MarcusMovieTime movie_time;
    MarcusScene scene;
    MarcusBackground background;
    MarcusProp prop_asset;
    MarcusObject added_prop, second_prop;
    MarcusObject move_before, move_first, move_later, move_other;
    unsigned long move_document;
    MarcusObject first, later, prop, inactive;
    MarcusBounds first_bounds, bounds;
    MarcusAnimationInfo animation;
    MarcusActionSpan action_span;
    MarcusAnimationEvent animation_event;
    MarcusImage image, pick_image, thumbnail;
    MarcusCamera camera, moved;
    MarcusProjection authored, orbit, manual, flight, panned;
    MarcusProjection guide_projection;
    float guide_depth[2];
    const long frames[] = {1, 5, 10, 16, 25, 30, 1};
    unsigned char *pixels;
    size_t bytes;
    int index, scene_count, picked, pick_x, pick_y, x, y, hits, renderer, expected_pick, span_index, event_index,
        active, prop_count, object_count, added_index, second_index;
    if (argc != 9)
        return 2;
    player = marcus_create();
    other = NULL;
    input = NULL;
    pixels = NULL;
    CHECK(player);
    CHECK(check_live_document(player));
    CHECK(check_sound_sheet(player));
    CHECK(check_caption_runs(player));
    CHECK(check_caption_rasters(player));
    CHECK(check_caption_faces(player));
    CHECK(!marcus_info(player, &info));
    CHECK(!marcus_movie_time(player, &movie_time) && movie_time.position_seconds == 0 &&
          movie_time.duration_seconds == 0);
    CHECK(!marcus_seek_time(player, 0));
    CHECK(!marcus_seek(player, 1));
    CHECK(!marcus_scene(player, -1, &scene));
    CHECK(!marcus_object_bounds(player, 0, &bounds) && !bounds.valid);
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]));
    CHECK(marcus_info(player, &info));
    scene_count = info.scene_count;
    CHECK(info.scene_count == 2 && info.object_count == 4 && info.first_frame == 1 && info.last_frame == 30);
    CHECK(info.sound_count == 0 && info.sound_track_count == 0);
    CHECK(marcus_movie_time(player, &movie_time) && movie_time.position_seconds == 0 &&
          fabs(movie_time.duration_seconds - 10) < 1e-6);
    CHECK(marcus_scene(player, 0, &scene) && scene.first_frame == 1 && scene.last_frame == 30);
    CHECK(scene.transition == MARCUS_TRANSITION_CUT);
    CHECK(marcus_scene(player, 1, &scene) && scene.transition == MARCUS_TRANSITION_CUT);
    CHECK(!marcus_scene_thumbnail(player, -1, &thumbnail) && !thumbnail.rgba && !thumbnail.width);
    CHECK(marcus_scene_thumbnail(player, 0, &thumbnail));
    CHECK(thumbnail.rgba && thumbnail.width == 8 && thumbnail.height == 6 && thumbnail.pitch == 32);
    CHECK(thumbnail.rgba[3] == 255);
    CHECK(marcus_scene_thumbnail(player, 1, &thumbnail) && !thumbnail.rgba && !thumbnail.width);
    CHECK(marcus_info(player, &info) && info.scene == 0 && info.frame == 1);
    CHECK(marcus_object(player, 0, &first));
    CHECK(!strcmp(first.name, "Animated actor") && first.visible && !first.prop && first.has_lifetime);
    CHECK(marcus_object(player, 1, &prop) && prop.prop);
    CHECK(!marcus_object(player, -1, &later));
    CHECK(!marcus_object(player, 4, &later));
    CHECK(marcus_object_bounds(player, 0, &first_bounds) && first_bounds.valid);
    CHECK(marcus_object_bounds(player, 1, &bounds) && bounds.valid); /* Prop. */
    CHECK(marcus_object_bounds(player, 3, &bounds) && bounds.valid); /* 3D text. */
    CHECK(!marcus_object_bounds(player, -1, &bounds) && !bounds.valid);
    CHECK(!marcus_object_bounds(player, info.object_count, &bounds) && !bounds.valid);
    CHECK(!marcus_object_bounds(player, 0, NULL));
    for (index = 0; index < 24; ++index)
        CHECK(fabs(first_bounds.corners[index]) < 32767);
    CHECK(memcmp(first_bounds.corners, first_bounds.corners + 21, 3 * sizeof(float)));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(image.width == 480 && image.height == 360 && image.pitch == 480 * 4 && image.rgba);
    CHECK(image.projection.valid && image.scene_camera.valid && image.scene_camera.vertical_fov > 0);
    CHECK(image.scene_camera.far_clip > image.scene_camera.near_clip && image.scene_camera.display_depth > 0);
    CHECK(matrices_match(&image.projection));
    authored = image.scene_camera;
    guide_depth[0] = authored.display_depth;
    bytes = (size_t)image.pitch * image.height;
    pixels = (unsigned char *)malloc(bytes);
    CHECK(pixels);
    memcpy(pixels, image.rgba, bytes);
    CHECK(marcus_animation_info(player, 0, &animation) && animation.span_count >= 1 && animation.event_count >= 5);
    CHECK(marcus_action_span(player, 0, 0, &action_span) && action_span.first_frame == 1 && action_span.name[0]);
    CHECK(action_span.action == first.action);
    hits = 0;
    for (index = 0; index < animation.event_count; ++index)
    {
        CHECK(marcus_animation_event(player, 0, index, &animation_event));
        if (animation_event.kind == MARCUS_ANIMATION_COSTUME)
        {
            CHECK(animation_event.frame == 8 || animation_event.frame == 20);
            ++hits;
        }
    }
    CHECK(hits == 2 && !memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_info(player, &info) && info.frame == 1 && info.camera == 0);
    CHECK(!marcus_animation_info(player, -1, &animation));
    CHECK(!marcus_action_span(player, 0, -1, &action_span));
    CHECK(!marcus_animation_event(player, 0, -1, &animation_event));
    /* Picking uses visible faces in the rendered camera, retains object-list
     * identities, and does not invalidate pixels or alter the evaluated frame. */
    pick_image = image;
    pick_image.rgba = NULL;
    CHECK(marcus_pick(player, &pick_image, -1, 0, &picked) && picked == -1);
    CHECK(marcus_pick(player, &pick_image, image.width, 0, &picked) && picked == -1);
    hits = 0;
    pick_x = pick_y = -1;
    expected_pick = -1;
    for (y = 6; y < image.height; y += 18)
        for (x = 6; x < image.width; x += 18)
        {
            CHECK(marcus_pick(player, &pick_image, x, y, &picked));
            if (picked >= 0)
            {
                CHECK(marcus_object(player, picked, &later) && later.visible);
                CHECK(later.actor_id == picked); /* Fixture IDs follow object-list order. */
                ++hits;
                pick_x = x;
                pick_y = y;
                expected_pick = picked;
            }
        }
    CHECK(hits > 0 && pick_x >= 0);
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_info(player, &info) && info.frame == 1 && info.camera == 0);
    for (renderer = MARCUS_RENDERER_BRENDER_COMPUTE; renderer < MARCUS_RENDERER_COUNT; ++renderer)
    {
        if (!marcus_set_renderer(player, renderer))
        {
            fprintf(stderr, "Skipping %s picking: %s\n", marcus_renderer_name(renderer), marcus_error(player));
            continue;
        }
        CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
        CHECK(marcus_pick(player, &image, pick_x, pick_y, &picked) && picked == expected_pick);
    }
    CHECK(marcus_set_renderer(player, MARCUS_RENDERER_BRENDER));
    CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    CHECK(marcus_pick(player, &pick_image, pick_x, pick_y, &picked) && picked == expected_pick);
    CHECK(!marcus_pick(player, NULL, 0, 0, &picked) && picked == -1);
    CHECK(!marcus_pick(player, &pick_image, 0, 0, NULL));
    CHECK(marcus_seek(player, 10));
    CHECK(!marcus_pick(player, &pick_image, pick_x, pick_y, &picked) && picked == -1);
    CHECK(marcus_seek(player, 10) && marcus_object(player, 0, &later));
    CHECK(later.x != first.x || later.y != first.y || later.z != first.z || later.cel != first.cel);
    CHECK(marcus_object_bounds(player, 0, &bounds) && bounds.valid);
    CHECK(memcmp(first_bounds.corners, bounds.corners, sizeof(bounds.corners)));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(image.scene_camera.display_depth == guide_depth[0]);
    CHECK(memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_seek(player, 1));
    CHECK(marcus_object_bounds(player, 0, &bounds) && bounds.valid);
    CHECK(!memcmp(first_bounds.corners, bounds.corners, sizeof(bounds.corners)));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(!memcmp(authored.transform, image.scene_camera.transform, sizeof(authored.transform)));
    CHECK(marcus_seek(player, 16) && marcus_info(player, &info) && info.camera == 1);
    CHECK(marcus_movie_time(player, &movie_time) && fabs(movie_time.position_seconds - 2.5) < 1e-6);
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(image.scene_camera.valid &&
          (memcmp(authored.transform, image.scene_camera.transform, sizeof(authored.transform)) ||
           authored.vertical_fov != image.scene_camera.vertical_fov));
    guide_depth[1] = image.scene_camera.display_depth;
    CHECK(marcus_seek(player, 1));
    CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    orbit = image.projection;
    for (index = 0; index < (int)(sizeof(frames) / sizeof(frames[0])); ++index)
    {
        CHECK(marcus_seek(player, frames[index]));
        CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
        CHECK(!memcmp(orbit.transform, image.projection.transform, sizeof(orbit.transform)));
        CHECK(image.projection.near_clip > 0 && image.projection.far_clip > image.projection.near_clip &&
              image.projection.far_clip <= 30000);
        CHECK(marcus_info(player, &info));
        CHECK(image.scene_camera.display_depth == guide_depth[info.camera]);
    }
    /* Rewinding restores the fitted interval as well as the camera pose. */
    CHECK(orbit.near_clip == image.projection.near_clip && orbit.far_clip == image.projection.far_clip);
    CHECK(marcus_render(player, 480, 360, 0, 65, 5, 1.5f, &image));
    manual = image.projection;
    CHECK(memcmp(orbit.transform, manual.transform, sizeof(orbit.transform)));
    CHECK(marcus_seek(player, 10) && marcus_render(player, 480, 360, 0, 65, 5, 1.5f, &image));
    CHECK(!memcmp(manual.transform, image.projection.transform, sizeof(manual.transform)));
    CHECK(marcus_pan_orbit(player, -0.1f, 0.15f, 65, 5, 1.5f));
    CHECK(marcus_render(player, 480, 360, 0, 65, 5, 1.5f, &image));
    panned = image.projection;
    CHECK(!memcmp(manual.transform, panned.transform, 9 * sizeof(float)));
    CHECK(memcmp(manual.transform + 9, panned.transform + 9, 3 * sizeof(float)));
    CHECK(panned.near_clip > 0 && panned.far_clip > panned.near_clip && panned.far_clip <= 30000);
    CHECK(marcus_seek(player, 25) && marcus_render(player, 480, 360, 0, 65, 5, 1.5f, &image));
    CHECK(!memcmp(panned.transform, image.projection.transform, sizeof(panned.transform)));
    CHECK(marcus_seek(player, 1));
    marcus_reset_framing(player);
    CHECK(!marcus_pan_orbit(player, 0.1f, 0, 25, 12, 1));
    CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    CHECK(!memcmp(orbit.transform, image.projection.transform, sizeof(orbit.transform)));
    /* Selection orbit follows posed bounds while retaining orientation. */
    CHECK(!marcus_set_orbit_target(player, -2));
    CHECK(!marcus_set_orbit_target(player, info.object_count));
    CHECK(marcus_set_orbit_target(player, 0));
    CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    CHECK(marcus_object_bounds(player, 0, &bounds) && orbit_centred(&image.projection, &bounds));
    CHECK(memcmp(orbit.transform + 9, image.projection.transform + 9, 3 * sizeof(float)));
    manual = image.projection;
    CHECK(marcus_seek(player, 10) && marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    CHECK(marcus_object_bounds(player, 0, &bounds) && orbit_centred(&image.projection, &bounds));
    CHECK(!memcmp(manual.transform, image.projection.transform, 9 * sizeof(float)));
    CHECK(memcmp(manual.transform + 9, image.projection.transform + 9, 3 * sizeof(float)));
    CHECK(marcus_set_orbit_target(player, 1));
    CHECK(marcus_render(player, 480, 360, 0, 65, 5, 1, &image));
    CHECK(marcus_object_bounds(player, 1, &bounds) && orbit_centred(&image.projection, &bounds));
    CHECK(marcus_seek(player, 1) && marcus_set_orbit_target(player, -1));
    CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    CHECK(!memcmp(orbit.transform, image.projection.transform, sizeof(orbit.transform)));
    CHECK(marcus_seek(player, 1) && marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    camera = image.camera;
    CHECK(camera.move_speed > 0);
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(fabs(image.camera.x - camera.x) < 0.001 && fabs(image.camera.z - camera.z) < 0.001);
    flight = image.projection;
    memcpy(pixels, image.rgba, bytes);
    moved = camera;
    moved.x += camera.move_speed * 0.5f;
    CHECK(marcus_render_fly(player, 480, 360, &moved, &image));
    CHECK(memcmp(pixels, image.rgba, bytes));
    moved = camera;
    moved.yaw += 20;
    CHECK(marcus_render_fly(player, 480, 360, &moved, &image));
    CHECK(memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_render_fly(player, 480, 360, &image.camera, &image));
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_seek(player, 10));
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(fabs(image.camera.x - camera.x) < 0.001 && fabs(image.camera.z - camera.z) < 0.001);
    CHECK(image.projection.near_clip > 0 && image.projection.far_clip > image.projection.near_clip &&
          image.projection.far_clip <= 30000);
    CHECK(image.scene_camera.display_depth == guide_depth[0]);
    CHECK(marcus_seek(player, 20) && marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(image.projection.near_clip > 0 && image.projection.far_clip > image.projection.near_clip &&
          image.projection.far_clip <= 30000);
    CHECK(image.scene_camera.display_depth == guide_depth[1]);
    CHECK(marcus_seek(player, 1));
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(flight.near_clip == image.projection.near_clip && flight.far_clip == image.projection.far_clip);
    /* Both views share a frame, but moving the work camera must not alter the
     * movie camera, its pixels, or the authored camera selection. */
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    memcpy(pixels, image.rgba, bytes);
    authored = image.scene_camera;
    moved = camera;
    moved.x += camera.move_speed;
    moved.yaw += 25;
    CHECK(marcus_render_fly(player, 480, 360, &moved, &image));
    CHECK(marcus_info(player, &info) && info.frame == 1 && info.camera == 0);
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(!memcmp(authored.transform, image.scene_camera.transform, sizeof(authored.transform)));
    /* Reset inspection to the shot, preserving its pose and lens instead of
     * fitting all actor bounds. Orbit and Fly must start at that same pose. */
    CHECK(marcus_match_camera(player, 480, 360, &camera));
    CHECK(fabs(camera.x - authored.transform[9]) < .001);
    CHECK(fabs(camera.y - authored.transform[10]) < .001);
    CHECK(fabs(camera.z - authored.transform[11]) < .001);
    CHECK(marcus_render(player, 480, 360, 0, camera.yaw, camera.pitch, 1, &image));
    manual = image.projection;
    for (index = 0; index < 12; ++index)
        CHECK(fabs(manual.transform[index] - authored.transform[index]) < .02);
    CHECK(manual.near_clip > 0 && manual.far_clip > manual.near_clip && manual.far_clip <= 30000);
    CHECK(fabs(manual.vertical_fov - 2 * atan(tan(authored.vertical_fov * .00872664626) *
               (authored.aspect > 4.0 / 3 ? authored.aspect / (4.0 / 3) : 1)) * 57.2957795) < .02);
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    for (index = 0; index < 12; ++index)
        CHECK(fabs(manual.transform[index] - image.projection.transform[index]) < .02);
    CHECK(fabs(manual.vertical_fov - image.projection.vertical_fov) < .001);
    hits = 0;
    for (y = 12; y < image.height; y += 24)
        for (x = 12; x < image.width; x += 24)
        {
            CHECK(marcus_pick(player, &image, x, y, &picked));
            if (picked >= 0)
            {
                CHECK(marcus_object(player, picked, &later) && later.visible);
                ++hits;
            }
        }
    CHECK(hits > 0); /* Fly uses its rendered pose rather than the movie's projection. */
    moved = camera;
    moved.x += 5;
    CHECK(marcus_render_fly(player, 480, 360, &moved, &image));
    CHECK(marcus_info(player, &info) && info.frame == 1 && info.camera == 0);
    CHECK(marcus_match_camera(player, 480, 360, &camera));
    CHECK(fabs(camera.x - authored.transform[9]) < .001);
    CHECK(!marcus_match_camera(player, 1, 1, &camera));
    CHECK(!marcus_match_camera(player, 480, 360, NULL));
    CHECK(marcus_seek(player, 20) && marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    authored = image.scene_camera;
    CHECK(marcus_match_camera(player, 480, 360, &camera));
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    for (index = 0; index < 12; ++index)
        CHECK(fabs(image.projection.transform[index] - authored.transform[index]) < .02);
    CHECK(marcus_render(player, 480, 360, 0, camera.yaw, camera.pitch, 1, &image));
    manual = image.projection;
    CHECK(marcus_pan_orbit(player, .1f, .1f, camera.yaw, camera.pitch, 1));
    CHECK(marcus_render(player, 480, 360, 0, camera.yaw, camera.pitch, 1, &image));
    CHECK(!memcmp(manual.transform, image.projection.transform, 9 * sizeof(float)));
    CHECK(memcmp(manual.transform + 9, image.projection.transform + 9, 3 * sizeof(float)));
    CHECK(marcus_seek(player, 1));
    marcus_reset_framing(player);
    moved = camera;
    moved.x = 40000;
    CHECK(!marcus_render_fly(player, 480, 360, &moved, &image) && !image.rgba);
    CHECK(!marcus_render_fly(player, 480, 360, NULL, &image) && !image.rgba);
    CHECK(!marcus_seek(player, 31) && marcus_info(player, &info) && info.frame == 1);
    CHECK(!marcus_select_scene(player, -1) && marcus_info(player, &info));
    CHECK(!marcus_select_scene(player, scene_count) && marcus_info(player, &info));
    CHECK(!marcus_open(player, "nonexistent-marcus-movie.3mm", argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == 2 && info.frame == 1);
    other = marcus_create();
    CHECK(other && !marcus_open(other, argv[1], argv[2], argv[3]));
    marcus_destroy(other);
    other = NULL;
    for (index = 0; index < scene_count; ++index)
    {
        CHECK(marcus_select_scene(player, index));
        CHECK(marcus_info(player, &info) && info.scene == index);
        CHECK(marcus_seek(player, info.last_frame));
        CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    }
    CHECK(!marcus_render(player, 1, 1, 0, 0, 0, 1, &image) && !image.rgba);
    CHECK(marcus_open(player, argv[8], argv[2], argv[3]));
    CHECK(marcus_scene(player, 0, &scene) && scene.transition == MARCUS_TRANSITION_DISSOLVE);
    CHECK(marcus_scene(player, 1, &scene) && scene.transition == MARCUS_TRANSITION_CUT);
    CHECK(marcus_info(player, &info) && info.scene == 0 && info.frame == 1);
    marcus_destroy(player);
    player = marcus_create();
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &pick_image));
    CHECK(marcus_open(player, argv[5], argv[2], argv[3]));
    CHECK(!marcus_pick(player, &pick_image, 240, 180, &picked) && picked == -1);
    CHECK(marcus_info(player, &info) && info.scene_count == 2 && info.object_count == 4);
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    input = marcus_input_create();
    CHECK(input && marcus_input_open(input, argv[6]) && marcus_input_count(input) == 2);
    CHECK(!marcus_open(player, argv[6], argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.object_count == 4);
    CHECK(marcus_open_input(player, input, 1, argv[2], argv[3]));
    marcus_input_destroy(input);
    input = NULL;
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(marcus_info(player, &info) && info.object_count == 5);
    CHECK(marcus_open(player, argv[4], argv[2], argv[3]));
    CHECK(marcus_movie_time(player, &movie_time) && movie_time.position_seconds == 0 &&
          fabs(movie_time.duration_seconds - 20.0 / 6) < 1e-6);
    CHECK(marcus_object(player, 4, &inactive) && !inactive.visible && !inactive.has_lifetime);
    CHECK(marcus_object_bounds(player, 4, &bounds) && !bounds.valid);
    CHECK(marcus_animation_info(player, 4, &animation) && !animation.span_count && !animation.event_count);
    CHECK(marcus_object(player, 1, &prop) && prop.last_frame > 10);
    CHECK(marcus_render(player, 480, 960, 1, 0, 0, 1, &image));
    CHECK(marcus_pick(player, &image, 240, 0, &picked) && picked == -1); /* Letterbox. */
    CHECK(marcus_select_scene(player, 1));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(marcus_pick(player, &image, 240, 180, &picked) && picked == -1);
    CHECK(marcus_match_camera(player, 480, 360, &camera));
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(marcus_pick(player, &image, 240, 180, &picked) && picked == -1); /* Scenery is not an actor. */
    CHECK(marcus_open(player, argv[7], argv[2], argv[3]));
    CHECK(marcus_animation_info(player, 1, &animation) && animation.event_count == 17);
    CHECK(marcus_animation_event(player, 1, 3, &animation_event) && animation_event.kind == MARCUS_ANIMATION_FREEZE &&
          animation_event.frame == 4);
    CHECK(marcus_animation_event(player, 1, 7, &animation_event) && animation_event.kind == MARCUS_ANIMATION_STEP &&
          animation_event.frame == 11);
    CHECK(marcus_animation_event(player, 1, 8, &animation_event) && animation_event.kind == MARCUS_ANIMATION_ROTATION &&
          animation_event.frame == 12);
    CHECK(marcus_animation_event(player, 1, 9, &animation_event) && animation_event.kind == MARCUS_ANIMATION_STEP &&
          animation_event.frame == 13);
    CHECK(marcus_animation_event(player, 1, 16, &animation_event) && animation_event.kind == MARCUS_ANIMATION_COSTUME &&
          animation_event.frame == 23);
    for (index = 1; index <= 30; ++index)
    {
        CHECK(marcus_seek(player, index) && marcus_object(player, 1, &prop));
        active = 0;
        for (span_index = 0; span_index < animation.span_count; ++span_index)
        {
            CHECK(marcus_action_span(player, 1, span_index, &action_span));
            if (index >= action_span.first_frame && index < action_span.end_frame)
            {
                ++active;
                CHECK(prop.visible && prop.action == action_span.action);
                CHECK(action_span.frozen == (index >= 4 && index < 6));
            }
        }
        CHECK(active == (prop.visible ? 1 : 0));
        CHECK(prop.visible == (index < 17 || index >= 22));
    }
    CHECK(marcus_seek(player, 1) && marcus_object(player, 1, &first));
    CHECK(marcus_animation_info(player, 1, &animation) && animation.event_count == 17);
    event_index = animation.event_count;
    CHECK(marcus_select_scene(player, 1));
    CHECK(marcus_animation_info(player, 1, &animation) && animation.event_count == event_index);
    /* Scene creation uses a private copy, retains existing scenes and supports
     * both reload and a fresh empty movie. No application UI is involved. */
    CHECK(marcus_background_count(player, argv[3]) > 0);
    CHECK(marcus_background(player, 0, &background));
    CHECK(!marcus_background(player, -1, &background));
    CHECK(marcus_info(player, &info));
    scene_count = info.scene_count;
    CHECK(!marcus_add_scene(player, 0xffffffffUL, argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == scene_count);
    CHECK(marcus_add_scene(player, background.chunk, argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == scene_count + 1 && info.scene == scene_count &&
          info.first_frame == 1 && info.last_frame == 1 && info.object_count == 0 && info.sound_count == 0);
    CHECK(marcus_render(player, 480, 270, 1, 0, 0, 1, &image) && image.rgba);
    CHECK(marcus_reload(player, argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == scene_count + 1 && info.scene == scene_count);
    CHECK(marcus_select_scene(player, 0) && marcus_object(player, 1, &first));
    CHECK(marcus_add_scene(player, background.chunk, argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == scene_count + 2 && info.scene == scene_count + 1);
    CHECK(marcus_open(player, argv[7], argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == scene_count);
    CHECK(marcus_open(player, argv[5], argv[2], argv[3]));
    CHECK(marcus_add_scene(player, background.chunk, argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == scene_count + 1);
    CHECK(marcus_open(player, argv[5], argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == scene_count);
    CHECK(marcus_open(player, argv[4], argv[2], argv[3]));
    CHECK(marcus_add_scene(player, background.chunk, argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == scene_count + 1);
    CHECK(marcus_select_scene(player, 0) && marcus_render(player, 480, 270, 1, 0, 0, 1, &image));
    marcus_destroy(player);
    player = marcus_create();
    CHECK(player && marcus_add_scene(player, background.chunk, argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == 1 && info.frame == 1 && info.object_count == 0);
    CHECK(marcus_render(player, 480, 270, 1, 0, 0, 1, &image) && image.rgba);
    CHECK(marcus_movie_time(player, &movie_time) && fabs(movie_time.duration_seconds - 1.0 / 6) < 1e-6);
    /* Whole-track prop translation, transient cancellation, bounds, persistence,
     * view retention and copy-on-write isolation of a shared ACTR between scenes. */
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]) && marcus_seek(player, 1));
    CHECK(marcus_object(player, 1, &move_first));
    CHECK(marcus_seek(player, 20) && marcus_object(player, 1, &move_later));
    CHECK(marcus_select_scene(player, 1) && marcus_seek(player, 10) && marcus_object(player, 1, &move_other));
    CHECK(marcus_select_scene(player, 0) && marcus_seek(player, 10) && marcus_object(player, 1, &move_before));
    move_document = marcus_document(player);
    CHECK(!marcus_preview_prop_position(player, 0, 0, 0, 0)); /* Actors. */
    CHECK(!marcus_preview_prop_position(player, 3, 0, 0, 0)); /* 3D text. */
    CHECK(!marcus_move_prop(player, -1, 0, 0, 0));
    CHECK(!marcus_move_prop(player, 1, 1e30f, 0, 0));
    CHECK(!marcus_preview_prop_position(player, 1, (float)HUGE_VAL, 0, 0));
    CHECK(marcus_object_bounds(player, 1, &first_bounds));
    CHECK(marcus_preview_prop_position(player, 1, move_before.x + 3.5f, move_before.y - 2.25f, move_before.z + 5.125f));
    CHECK(marcus_object(player, 1, &later) && fabs(later.x - move_before.x - 3.5) < .0001 &&
          fabs(later.y - move_before.y + 2.25) < .0001 && fabs(later.z - move_before.z - 5.125) < .0001);
    CHECK(marcus_document(player) == move_document && marcus_object_bounds(player, 1, &bounds));
    for (index = 0; index < 8; ++index)
        CHECK(fabs(bounds.corners[index * 3] - first_bounds.corners[index * 3] - 3.5) < .001 &&
              fabs(bounds.corners[index * 3 + 1] - first_bounds.corners[index * 3 + 1] + 2.25) < .001 &&
              fabs(bounds.corners[index * 3 + 2] - first_bounds.corners[index * 3 + 2] - 5.125) < .001);
    CHECK(marcus_preview_prop_position(player, 1, move_before.x, move_before.y, move_before.z));
    CHECK(marcus_object(player, 1, &later) && fabs(later.x - move_before.x) < .0001);
    CHECK(marcus_preview_prop_position(player, 1, move_before.x + 7, move_before.y, move_before.z));
    CHECK(marcus_seek(player, 10) && marcus_object(player, 1, &later) && fabs(later.x - move_before.x) < .0001);
    CHECK(marcus_match_camera(player, 480, 270, &camera));
    CHECK(marcus_render(player, 480, 270, 0, 25, -10, 1.2f, &image));
    CHECK(marcus_pan_orbit(player, .1f, -.2f, 25, -10, 1.2f));
    CHECK(marcus_render(player, 480, 270, 0, 25, -10, 1.2f, &image));
    panned = image.projection;
    CHECK(matrices_match(&panned));
    manual = panned;
    manual.orthographic = 1;
    manual.ortho_height = 25;
    CHECK(matrices_match(&manual));
    manual.transform[0] = manual.transform[1] = manual.transform[2] = 0;
    CHECK(!matrices_match(&manual));
    CHECK(marcus_move_prop(player, 1, move_before.x + 3.5f, move_before.y - 2.25f, move_before.z + 5.125f));
    CHECK(marcus_document(player) != move_document && marcus_info(player, &info) && info.scene == 0 && info.frame == 10);
    CHECK(marcus_object(player, 1, &later) && later.actor_id == move_before.actor_id && later.chunk == move_before.chunk &&
          later.action == move_before.action && later.cel == move_before.cel && later.first_frame == move_before.first_frame &&
          later.last_frame == move_before.last_frame);
    CHECK(marcus_render(player, 480, 270, 0, 25, -10, 1.2f, &image));
    for (index = 0; index < 12; ++index) CHECK(fabs(image.projection.transform[index] - panned.transform[index]) < .001);
    CHECK(image.projection.vertical_fov == panned.vertical_fov);
    CHECK(marcus_scene_thumbnail(player, 0, &thumbnail) && !thumbnail.rgba);
    CHECK(marcus_seek(player, 1) && marcus_object(player, 1, &later) &&
          fabs(later.x - move_first.x - 3.5) < .0001 && fabs(later.y - move_first.y + 2.25) < .0001 &&
          fabs(later.z - move_first.z - 5.125) < .0001 && later.action == move_first.action && later.cel == move_first.cel);
    CHECK(marcus_seek(player, 20) && marcus_object(player, 1, &later) &&
          fabs(later.x - move_later.x - 3.5) < .0001 && fabs(later.y - move_later.y + 2.25) < .0001 &&
          fabs(later.z - move_later.z - 5.125) < .0001 && later.action == move_later.action && later.cel == move_later.cel);
    CHECK(marcus_select_scene(player, 1) && marcus_seek(player, 10) && marcus_object(player, 1, &later) &&
          later.chunk == move_other.chunk && later.x == move_other.x && later.y == move_other.y && later.z == move_other.z);
    CHECK(marcus_select_scene(player, 0) && marcus_reload(player, argv[2], argv[3]) && marcus_seek(player, 10));
    CHECK(marcus_object(player, 1, &later) && fabs(later.x - move_before.x - 3.5) < .0001);
    /* Camera thumbnails are isolated from the live render. Authored camera
     * changes replace same-frame choices and preserve later cuts. */
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]) && marcus_seek(player, 10));
    CHECK(marcus_info(player, &info) && info.camera == 0 && info.camera_count >= 2);
    CHECK(marcus_render(player, 160, 120, 1, 0, 0, 1, &image));
    free(pixels);
    bytes = (size_t)image.pitch * image.height;
    pixels = (unsigned char *)malloc(bytes);
    CHECK(pixels);
    memcpy(pixels, image.rgba, bytes);
    CHECK(marcus_camera_thumbnail(player, 1, &thumbnail) && thumbnail.width == 160 && thumbnail.rgba);
    for (index = 0; index < (int)info.camera_count; ++index)
    {
        CHECK(marcus_camera_projection(player, (unsigned long)index, &guide_projection) && guide_projection.valid);
        CHECK(marcus_camera_thumbnail(player, (unsigned long)index, &thumbnail));
        CHECK(!memcmp(guide_projection.transform, thumbnail.scene_camera.transform, sizeof(guide_projection.transform)));
        CHECK(guide_projection.vertical_fov == thumbnail.scene_camera.vertical_fov &&
              guide_projection.aspect == thumbnail.scene_camera.aspect &&
              guide_projection.near_clip == thumbnail.scene_camera.near_clip &&
              guide_projection.far_clip == thumbnail.scene_camera.far_clip && guide_projection.display_depth == 10);
    }
    CHECK(!marcus_camera_projection(player, info.camera_count, &guide_projection) && !guide_projection.valid);
    CHECK(marcus_info(player, &info) && info.frame == 10 && info.camera == 0);
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(!marcus_set_scene_camera(player, info.camera_count));
    CHECK(marcus_info(player, &info) && info.frame == 10 && info.camera == 0);
    CHECK(marcus_set_scene_camera(player, 1));
    CHECK(marcus_info(player, &info) && info.scene == 0 && info.frame == 10 && info.camera == 1);
    CHECK(marcus_scene_thumbnail(player, 0, &thumbnail) && thumbnail.rgba); /* First frame is unchanged. */
    CHECK(marcus_reload(player, argv[2], argv[3]) && marcus_seek(player, 10));
    CHECK(marcus_info(player, &info) && info.camera == 1);
    CHECK(marcus_seek(player, 9) && marcus_info(player, &info) && info.camera == 0);
    CHECK(marcus_seek(player, 10) && marcus_set_scene_camera(player, 0));
    CHECK(marcus_seek(player, 14) && marcus_info(player, &info) && info.camera == 0);
    CHECK(marcus_seek(player, 15) && marcus_info(player, &info) && info.camera == 1);
    CHECK(marcus_seek(player, 1) && marcus_set_scene_camera(player, 1));
    CHECK(marcus_scene_thumbnail(player, 0, &thumbnail) && !thumbnail.rgba); /* Stale saved preview removed. */
    CHECK(marcus_select_scene(player, 1) && marcus_select_scene(player, 0));
    CHECK(marcus_info(player, &info) && info.camera == 1);
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]) && marcus_seek(player, 10));
    CHECK(marcus_info(player, &info) && info.camera == 0);
    prop_count = marcus_prop_count(player, argv[3]);
    CHECK(prop_count > 0 && marcus_prop(player, 0, &prop_asset));
    CHECK(!marcus_prop(player, -1, &prop_asset));
    for (index = 0; index < prop_count; ++index)
    {
        CHECK(marcus_prop(player, index, &prop_asset));
        if (prop_asset.chunk == 0x1020) break;
    }
    CHECK(index < prop_count);
    CHECK(marcus_info(player, &info));
    object_count = info.object_count;
    CHECK(marcus_add_prop(player, 0xffffffffUL) < 0);
    CHECK(marcus_info(player, &info) && info.object_count == object_count && info.frame == 10);
    added_index = marcus_add_prop(player, prop_asset.chunk);
    CHECK(added_index >= 0 && marcus_info(player, &info) && info.object_count == object_count + 1 && info.frame == 10);
    CHECK(marcus_object(player, added_index, &added_prop) && added_prop.prop && !added_prop.text && added_prop.visible &&
          added_prop.first_frame == 10 && added_prop.last_frame == 30 && !strcmp(added_prop.name, prop_asset.name));
    CHECK(marcus_object_bounds(player, added_index, &bounds) && bounds.valid);
    CHECK(marcus_seek(player, 9) && marcus_object(player, added_index, &later) && !later.visible);
    CHECK(!marcus_move_prop(player, added_index, 0, 0, 0));
    CHECK(marcus_seek(player, 20) && marcus_object(player, added_index, &later) && later.visible &&
          fabs(later.x - added_prop.x) < 1e-4 && fabs(later.y - added_prop.y) < 1e-4 && fabs(later.z - added_prop.z) < 1e-4);
    second_index = marcus_add_prop(player, prop_asset.chunk);
    CHECK(second_index >= 0 && marcus_object(player, second_index, &second_prop) && second_prop.actor_id != added_prop.actor_id &&
          second_prop.first_frame == 20 && second_prop.visible);
    CHECK(marcus_render(player, 480, 270, 1, 0, 0, 1, &image));
    CHECK(marcus_reload(player, argv[2], argv[3]) && marcus_seek(player, 20));
    CHECK(marcus_object(player, added_index, &later) && later.actor_id == added_prop.actor_id && later.visible);
    CHECK(marcus_select_scene(player, 1) && marcus_info(player, &info) && info.object_count == object_count);
    CHECK(marcus_open(player, argv[5], argv[2], argv[3]) && marcus_seek(player, 10));
    CHECK(marcus_add_prop(player, prop_asset.chunk) >= 0 && marcus_render(player, 480, 270, 1, 0, 0, 1, &image));
    CHECK(marcus_open(player, argv[5], argv[2], argv[3]) && marcus_info(player, &info) && info.object_count == object_count);
    marcus_destroy(player);
    player = marcus_create();
    CHECK(player && marcus_add_scene(player, background.chunk, argv[2], argv[3]));
    added_index = marcus_add_prop(player, prop_asset.chunk);
    CHECK(added_index == 0 && marcus_object(player, added_index, &later) && later.prop && later.visible);
    CHECK(marcus_render(player, 480, 270, 1, 0, 0, 1, &image));
    /* Undo retains private leases, including ZIP members, and records completed
     * edits only. Each call restores the scene/frame where the edit began. */
    CHECK(marcus_can_undo(player) && marcus_modified(player));
    CHECK(marcus_undo(player) && marcus_info(player, &info) && info.object_count == 0 && info.scene_count == 1);
    CHECK(marcus_undo(player) && !marcus_info(player, &info) && !marcus_can_undo(player) && !marcus_modified(player));
    CHECK(!marcus_undo(player));
    CHECK(marcus_add_scene(player, background.chunk, argv[2], argv[3]) && marcus_undo(player));
    CHECK(!marcus_info(player, &info) && !marcus_can_undo(player));
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]) && marcus_seek(player, 10));
    CHECK(!marcus_can_undo(player) && !marcus_modified(player));
    CHECK(marcus_object(player, 1, &move_before));
    CHECK(marcus_render(player, 160, 90, 1, 0, 0, 1, &image));
    free(pixels);
    bytes = (size_t)image.pitch * image.height;
    pixels = (unsigned char *)malloc(bytes);
    CHECK(pixels);
    memcpy(pixels, image.rgba, bytes);
    CHECK(marcus_preview_prop_position(player, 1, move_before.x + 2, move_before.y, move_before.z));
    CHECK(!marcus_can_undo(player) && !marcus_modified(player));
    CHECK(marcus_preview_prop_position(player, 1, move_before.x, move_before.y, move_before.z));
    CHECK(marcus_move_prop(player, 1, move_before.x, move_before.y, move_before.z));
    CHECK(marcus_add_prop(player, 0xffffffffUL) < 0 && !marcus_can_undo(player));
    CHECK(marcus_match_camera(player, 480, 270, &camera));
    CHECK(marcus_render(player, 480, 270, 0, 25, -10, 1.2f, &image));
    panned = image.projection;
    CHECK(marcus_move_prop(player, 1, move_before.x + 3, move_before.y, move_before.z));
    CHECK(marcus_move_prop(player, 1, move_before.x + 8, move_before.y, move_before.z));
    CHECK(marcus_can_undo(player) && marcus_modified(player));
    CHECK(marcus_undo(player) && marcus_info(player, &info) && info.frame == 10 && info.scene == 0);
    CHECK(marcus_object(player, 1, &later) && fabs(later.x - move_before.x - 3) < .0001);
    CHECK(marcus_render(player, 480, 270, 0, 25, -10, 1.2f, &image));
    for (index = 0; index < 12; ++index) CHECK(fabs(image.projection.transform[index] - panned.transform[index]) < .001);
    CHECK(marcus_modified(player) && marcus_can_undo(player));
    CHECK(marcus_reload(player, argv[2], argv[3]) && marcus_can_undo(player) && marcus_modified(player));
    CHECK(marcus_undo(player) && !marcus_can_undo(player) && !marcus_modified(player));
    CHECK(marcus_object(player, 1, &later) && later.chunk == move_before.chunk && later.x == move_before.x &&
          later.y == move_before.y && later.z == move_before.z);
    CHECK(marcus_render(player, 160, 90, 1, 0, 0, 1, &image) && !memcmp(pixels, image.rgba, bytes));
    added_index = marcus_add_prop(player, prop_asset.chunk);
    CHECK(added_index >= 0 && marcus_set_scene_camera(player, 1));
    CHECK(marcus_add_scene(player, background.chunk, argv[2], argv[3]));
    CHECK(marcus_undo(player) && marcus_info(player, &info) && info.scene_count == 2 && info.scene == 0 &&
          info.frame == 10 && info.object_count == 5 && info.camera == 1);
    CHECK(marcus_select_scene(player, 1) && marcus_seek(player, 20)); /* Navigation is not an undo step. */
    CHECK(!marcus_set_scene_camera(player, 0xffffffffUL)); /* Failed edits do not consume an undo step. */
    CHECK(marcus_undo(player) && marcus_info(player, &info) && info.scene == 0 && info.frame == 10 && info.camera == 0);
    CHECK(marcus_object(player, added_index, &later) && later.visible && marcus_modified(player));
    CHECK(marcus_undo(player) && marcus_info(player, &info) && info.object_count == 4);
    CHECK(!marcus_can_undo(player) && !marcus_modified(player));
    CHECK(marcus_render(player, 160, 90, 1, 0, 0, 1, &image) && !memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_add_prop(player, prop_asset.chunk) >= 0);
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]) && !marcus_can_undo(player) && !marcus_modified(player));
    CHECK(marcus_open(player, argv[5], argv[2], argv[3]) && marcus_seek(player, 10));
    CHECK(marcus_add_prop(player, prop_asset.chunk) >= 0 && marcus_reload(player, argv[2], argv[3]));
    CHECK(marcus_undo(player) && marcus_info(player, &info) && info.object_count == 4 && info.frame == 10);
    CHECK(marcus_render(player, 160, 90, 1, 0, 0, 1, &image) && !memcmp(pixels, image.rgba, bytes));
    CHECK(!marcus_can_undo(player) && !marcus_modified(player));
    /* Edge extension uses the retail rule: no retiming, and the opening
     * camera moves with a prepended boundary. Each click is one undo step. */
    {
        MarcusInfo original_range, extended;
        MarcusScene other_scene, unchanged_scene;
        MarcusObject original_actor, placed_actor;
        MarcusMovieTime original_time, extended_time;
        unsigned long opening_camera;
        CHECK(marcus_open(player, argv[1], argv[2], argv[3]) && marcus_seek(player, 10));
        CHECK(!marcus_add_frame(player, -1) && !marcus_add_frame(player, 1));
        CHECK(!marcus_add_frame(player, 0) && !marcus_add_frame(player, 2));
        CHECK(!marcus_can_undo(player) && !marcus_modified(player));
        CHECK(marcus_select_scene(player, 1) && marcus_seek(player, 1) && marcus_info(player, &original_range));
        CHECK(marcus_scene(player, 0, &other_scene) && marcus_movie_time(player, &original_time));
        opening_camera = original_range.camera;
        CHECK(marcus_object(player, 0, &original_actor) && original_actor.visible);
        CHECK(marcus_move_object(player, 0, original_actor.x + 2, original_actor.y, original_actor.z));
        CHECK(marcus_add_frame(player, -1) && marcus_info(player, &extended));
        CHECK(extended.first_frame == original_range.first_frame - 1 &&
              extended.last_frame == original_range.last_frame && extended.frame == extended.first_frame &&
              extended.camera == opening_camera && marcus_modified(player));
        CHECK(marcus_object(player, 0, &placed_actor) && !placed_actor.visible);
        CHECK(marcus_add_frame(player, -1) && marcus_info(player, &extended) && extended.first_frame == -1 &&
              extended.frame == -1 && extended.camera == opening_camera);
        CHECK(marcus_undo(player) && marcus_info(player, &extended) && extended.first_frame == 0 && extended.frame == 0);
        CHECK(marcus_seek(player, 1) && marcus_object(player, 0, &placed_actor));
        CHECK(placed_actor.visible && fabs(placed_actor.x - original_actor.x - 2) < .001 &&
              placed_actor.y == original_actor.y && placed_actor.z == original_actor.z &&
              placed_actor.first_frame == original_actor.first_frame && placed_actor.action == original_actor.action &&
              placed_actor.cel == original_actor.cel);
        CHECK(marcus_seek(player, 14) && marcus_info(player, &extended) && extended.camera == opening_camera);
        CHECK(marcus_seek(player, 15) && marcus_info(player, &extended) && extended.camera == 0);
        CHECK(marcus_seek(player, original_range.last_frame) && marcus_add_frame(player, 1));
        CHECK(marcus_info(player, &extended) && extended.last_frame == original_range.last_frame + 1 &&
              extended.frame == extended.last_frame && extended.first_frame == original_range.first_frame - 1);
        CHECK(marcus_add_frame(player, 1) && marcus_info(player, &extended) &&
              extended.last_frame == original_range.last_frame + 2 && extended.frame == extended.last_frame);
        CHECK(marcus_scene(player, 0, &unchanged_scene) && unchanged_scene.first_frame == other_scene.first_frame &&
              unchanged_scene.last_frame == other_scene.last_frame);
        CHECK(marcus_movie_time(player, &extended_time) && extended_time.duration_seconds > original_time.duration_seconds);
        CHECK(marcus_reload(player, argv[2], argv[3]) && marcus_info(player, &extended) &&
              extended.first_frame == original_range.first_frame - 1 && extended.last_frame == original_range.last_frame + 2);
        CHECK(marcus_undo(player) && marcus_info(player, &extended) &&
              extended.last_frame == original_range.last_frame + 1 && extended.frame == original_range.last_frame + 1);
        CHECK(marcus_undo(player) && marcus_info(player, &extended) &&
              extended.first_frame == original_range.first_frame - 1 && extended.last_frame == original_range.last_frame &&
              extended.frame == original_range.last_frame);
        CHECK(marcus_undo(player) && marcus_info(player, &extended) && extended.first_frame == original_range.first_frame &&
              extended.last_frame == original_range.last_frame && extended.frame == original_range.first_frame);
        CHECK(marcus_object(player, 0, &placed_actor) && fabs(placed_actor.x - original_actor.x - 2) < .001);
        CHECK(marcus_undo(player) && marcus_object(player, 0, &placed_actor) && placed_actor.x == original_actor.x &&
              !marcus_can_undo(player) && !marcus_modified(player));
        /* A new one-frame scene supports either edge independently. */
        CHECK(marcus_add_scene(player, background.chunk, argv[2], argv[3]) && marcus_info(player, &extended) &&
              extended.first_frame == extended.last_frame);
        CHECK(marcus_add_frame(player, 1) && marcus_info(player, &extended) && extended.first_frame == 1 && extended.last_frame == 2);
        CHECK(marcus_undo(player) && marcus_add_frame(player, -1) && marcus_info(player, &extended) &&
              extended.first_frame == 0 && extended.last_frame == 1 && extended.frame == 0);
        CHECK(marcus_undo(player) && marcus_undo(player) && !marcus_modified(player));
    }
    marcus_destroy(player);
    free(pixels);
    puts("Marcus C API: scenes, actor/prop metadata, viewport picking, evaluated motion, image rewind, invalid input "
         "and renderer "
         "lifecycle passed.");
    return 0;
failed:
    marcus_input_destroy(input);
    free(pixels);
    marcus_destroy(other);
    marcus_destroy(player);
    return 1;
}
