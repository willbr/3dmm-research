#include "sdl_wave_device.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "C89 SDL WAVE device failed at line %d\n", __LINE__); exit(1); } } while (0)

static const MMAudioSample *read_sample(void *p)
{
    return p;
}

static void enter(void *p) { mm_sdl_wave_queue_enter(p); }
static void leave(void *p) { mm_sdl_wave_queue_leave(p); }

static void release(void *context, void *sample)
{
    (void)context;
    mm_audio_sample_release(sample);
}

static MMI32 cursor(MMSdlWaveQueue *queue)
{
    MMI32 result;
    mm_sdl_wave_queue_enter(queue);
    result = queue->entries->current;
    mm_sdl_wave_queue_leave(queue);
    return result;
}

int main(void)
{
    MMSdlWaveDevice *device, *replacement;
    MMSdlWaveQueue queue = {0};
    MMSoundQueue entries;
    MMSoundEntry entry = {0};
    MMSoundQueueServices services = {NULL, NULL, enter, leave, NULL, release, NULL};
    MMSdlWaveStats stats;
    MMAudioSample *sample;
    float *pcm;
    size_t i;
    MMU32 started;
    CHECK(!mm_sdl_wave_device_create(0));
    device = mm_sdl_wave_device_create(65536);
    CHECK(device && mm_sdl_wave_device_open(device));
    mm_sdl_wave_device_set_volume(device, -1);
    CHECK(!mm_sdl_wave_device_volume(device));
    mm_sdl_wave_device_set_volume(device, 32768);
    CHECK(mm_sdl_wave_device_volume(device) == 32768);
    CHECK(mm_sound_queue_init(&entries, NULL, 1, 0));
    CHECK(mm_sdl_wave_queue_init(&queue, device, &entries, read_sample));
    services.context = &queue;
    services.queue = &entries;

    pcm = malloc(9600 * sizeof(*pcm));
    CHECK(pcm);
    for (i = 0; i < 9600; ++i)
        pcm[i] = 0.25f;
    sample = mm_audio_sample_adopt(pcm, 4800, free);
    CHECK(sample);
    entry.object = sample;
    entry.id = 1;
    entry.volume = 65536;
    entry.plays = 1;
    mm_sdl_wave_note_decode();
    mm_sdl_wave_device_begin(device);
    mm_sdl_wave_queue_enter(&queue);
    CHECK(mm_array_add(&entries.rows, &entry, NULL) == MM_STORAGE_OK && mm_sdl_wave_queue_sync(&queue));
    mm_sdl_wave_queue_leave(&queue);
    mm_sdl_wave_device_release(device);
    SDL_Delay(125);
    mm_sdl_wave_stats(&stats);
    CHECK(stats.active && stats.decoded.low == 1 && !stats.started.low && !cursor(&queue));
    mm_sdl_wave_device_end(queue.device);
    started = (MMU32)SDL_GetTicks();
    while (!cursor(&queue) && (MMU32)SDL_GetTicks() - started < 4000)
        SDL_Delay(1);
    CHECK(cursor(&queue) == 1);
    mm_sdl_wave_stats(&stats);
    CHECK(stats.active && stats.started.low == 1 && stats.completed.low == 1);
    mm_sdl_wave_queue_flush(&queue, &services);
    CHECK(!mm_sound_queue_count(&entries) && !cursor(&queue));

    replacement = mm_sdl_wave_device_create(65536);
    CHECK(replacement && mm_sdl_wave_device_open(replacement));
    mm_sdl_wave_stats(&stats);
    CHECK(stats.active && stats.decoded.low == 1 && !stats.started.low);
    mm_sdl_wave_device_release(replacement);
    mm_sdl_wave_stats(&stats);
    CHECK(!stats.active && !stats.decoded.low);
    mm_sdl_wave_queue_dispose(&queue);
    CHECK(mm_storage_dispose(&entries.storage) == MM_STORAGE_OK);
    puts("C89 SDL WAVE device: queue ownership, grouped start, completion cursor, flush and device replacement passed.");
    return 0;
}
