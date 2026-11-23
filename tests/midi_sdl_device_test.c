#include "midi_sdl_device.h"
#include "mm/midi_render.h"
#include "mm/native_runtime.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "C89 SDL MIDI failed at line %d\n", __LINE__); exit(1); } } while (0)

typedef struct MidiReturns
{
    MMSdlMidiDevice *device;
    MMNativeEvent *ready;
    void *buffer;
    size_t tag;
    int count;
} MidiReturns;

static void returned(void *context, void *buffer, size_t tag)
{
    MidiReturns *results = context;
    MMSdlMidiStats stats;
    mm_sdl_midi_stats(results->device, &stats);
    CHECK(stats.active);
    results->buffer = buffer;
    results->tag = tag;
    ++results->count;
    CHECK(!mm_native_event_signal(results->ready));
}

static void expect_return(MidiReturns *results, void *buffer, size_t tag, int count)
{
    int signaled = 0;
    CHECK(!mm_native_event_wait(results->ready, 4000, &signaled) && signaled);
    CHECK(results->count == count && results->buffer == buffer && results->tag == tag);
}

int main(int argc, char **argv)
{
    MidiReturns results = {0};
    MMSdlMidiStats stats;
    MMMidiStreamEvent notes[] = {{0, 0, 0x00643c90UL}, {100, 0, 0x00003c80UL}};
    const size_t tag = (size_t)-6;
    CHECK(argc == 2);
    CHECK(!mm_sdl_midi_create(NULL, NULL, argv[1]));
    CHECK(!mm_sdl_midi_create(returned, &results, "missing-midi-device-test.sf2"));
    results.ready = mm_native_event_create(NULL);
    CHECK(results.ready);
    results.device = mm_sdl_midi_create(returned, &results, argv[1]);
    CHECK(results.device && !mm_sdl_midi_active(results.device));
    CHECK(!mm_sdl_midi_queue(results.device, notes, sizeof(notes), 0, 1, tag));
    mm_sdl_midi_set_volume(results.device, -1);
    CHECK(mm_sdl_midi_volume(results.device) == 0);
    mm_sdl_midi_set_volume(results.device, MM_SDL_MIDI_FULL_VOLUME);
    CHECK(mm_sdl_midi_activate(results.device, 1));
    CHECK(mm_sdl_midi_queue(results.device, notes, sizeof(notes), 0, 2, tag));
    expect_return(&results, notes, tag, 1);
    mm_sdl_midi_stats(results.device, &stats);
    CHECK(stats.active && stats.notes.low == 2 && stats.completed.low == 1 && stats.audible_frames.low > 0);
    CHECK(mm_sdl_midi_queue(results.device, notes, sizeof(notes), 0, 0, tag - 1));
    CHECK(!mm_sdl_midi_activate(results.device, 0));
    mm_sdl_midi_stop(results.device);
    expect_return(&results, notes, tag - 1, 2);
    CHECK(mm_sdl_midi_activate(results.device, 0) && !mm_sdl_midi_active(results.device));
    CHECK(mm_sdl_midi_activate(results.device, 1));
    CHECK(mm_sdl_midi_queue(results.device, notes, sizeof(notes), 0, 1, tag - 2));
    expect_return(&results, notes, tag - 2, 3);
    CHECK(mm_sdl_midi_activate(results.device, 0));
    mm_sdl_midi_destroy(results.device);
    mm_native_event_destroy(results.ready);
    puts("C89 SDL MIDI device: bank failure and retry, worker reentry, borrowed buffers, stop and reopen passed.");
    return 0;
}
