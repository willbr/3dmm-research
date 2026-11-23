/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_sdl_device.h"
#include "midi_render.h"
#include "midi_worker.h"
#include "sdl_audio_output.h"
#include "mm/native_runtime.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>

struct MMSdlMidiDevice
{
    MMMidiDeviceRenderer *renderer;
    MMSdlAudioOutput *output;
    MMMidiWorker *worker;
    MMNativeMutex *control;
    MMSdlMidiComplete complete;
    void *context;
    int active;
    MMI32 volume;
};

static void enter_control(MMSdlMidiDevice *device)
{
    if (mm_native_mutex_enter(device->control))
        abort();
}

static void leave_control(MMSdlMidiDevice *device)
{
    if (mm_native_mutex_leave(device->control))
        abort();
}

static MMU32 return_buffers(void *context)
{
    MMSdlMidiDevice *device = context;
    void *buffer;
    size_t tag;
    int returned;
    for (;;)
    {
        MMMidiRenderer *renderer = mm_midi_device_renderer_enter(device->renderer);
        returned = mm_midi_render_pop(renderer, &buffer, &tag);
        mm_midi_device_renderer_leave(device->renderer);
        if (!returned)
            break;
        device->complete(device->context, buffer, tag);
    }
    /* A seek prefix may complete before its main buffer is queued. The mixer
     * lock orders the submissions. Deliver its callback before going idle. */
    mm_midi_render_idle(mm_midi_device_renderer_enter(device->renderer));
    mm_midi_device_renderer_leave(device->renderer);
    return 0xffffffffUL;
}

static void render(void *context, float *buffer, size_t frames)
{
    MMSdlMidiDevice *device = context;
    int returned = mm_midi_render(mm_midi_device_renderer_enter(device->renderer), buffer, frames);
    mm_midi_device_renderer_leave(device->renderer);
    if (returned)
        mm_midi_worker_signal(device->worker);
}

static int load_bank(MMMidiDeviceRenderer *renderer, const char *path)
{
    static const char relative[] = "assets/GeneralUserGS.sf2";
    char *derived = NULL;
    size_t bytes = 0;
    void *bank;
    int loaded;
    if (!path || !*path)
        path = SDL_getenv("KAUAI_SOUNDFONT");
    if (!path || !*path)
    {
        const char *base = SDL_GetBasePath();
        size_t length;
        if (!base)
            return 0;
        length = SDL_strlen(base);
        if (length > (size_t)-1 - sizeof(relative))
            return 0;
        derived = SDL_malloc(length + sizeof(relative));
        if (!derived)
            return 0;
        SDL_memcpy(derived, base, length);
        SDL_memcpy(derived + length, relative, sizeof(relative));
        path = derived;
    }
    bank = SDL_LoadFile(path, &bytes);
    loaded = bank && mm_midi_device_renderer_load(renderer, bank, bytes, MM_AUDIO_MIX_RATE);
    if (!loaded)
    {
        SDL_SetError("Cannot load a complete General MIDI bank from %s", path);
        fprintf(stderr, "SDL MIDI: %s\n", SDL_GetError());
    }
    SDL_free(bank);
    SDL_free(derived);
    return loaded;
}

MMSdlMidiDevice *mm_sdl_midi_create(MMSdlMidiComplete complete, void *context, const char *path)
{
    MMSdlMidiDevice *device;
    if (!complete)
        return NULL;
    device = calloc(1, sizeof(*device));
    if (!device)
        return NULL;
    device->complete = complete;
    device->context = context;
    device->volume = MM_SDL_MIDI_FULL_VOLUME;
    device->control = mm_native_mutex_create(NULL);
    if (!device->control)
        goto fail;
    device->renderer = mm_midi_device_renderer_create();
    if (!device->renderer || !load_bank(device->renderer, path))
        goto fail;
    device->output = mm_sdl_audio_output_create(device, render, 512);
    if (!device->output)
        goto fail;
    device->worker = mm_midi_worker_start(return_buffers, device);
    if (device->worker)
        return device;
fail:
    mm_sdl_midi_destroy(device);
    return NULL;
}

void mm_sdl_midi_destroy(MMSdlMidiDevice *device)
{
    if (!device)
        return;
    if (device->output)
        mm_sdl_audio_output_close(device->output);
    mm_midi_worker_destroy(device->worker);
    mm_sdl_audio_output_destroy(device->output);
    mm_midi_device_renderer_destroy(device->renderer);
    mm_native_mutex_destroy(device->control);
    free(device);
}

int mm_sdl_midi_activate(MMSdlMidiDevice *device, int active)
{
    MMMidiRenderer *renderer;
    int ok = 1;
    enter_control(device);
    if (!active && device->active)
    {
        renderer = mm_midi_device_renderer_enter(device->renderer);
        ok = renderer->head == NULL;
        mm_midi_device_renderer_leave(device->renderer);
        if (ok)
        {
            mm_sdl_audio_output_close(device->output);
            device->active = 0;
        }
    }
    else if (active && !device->active)
    {
        SDL_SetMainReady();
        ok = mm_midi_render_reset(mm_midi_device_renderer_enter(device->renderer));
        mm_midi_device_renderer_leave(device->renderer);
        ok = ok && mm_sdl_audio_output_open(device->output, NULL, NULL);
        device->active = ok;
    }
    leave_control(device);
    return ok;
}

int mm_sdl_midi_active(MMSdlMidiDevice *device)
{
    int active;
    enter_control(device);
    active = device->active;
    leave_control(device);
    return active;
}

void mm_sdl_midi_set_volume(MMSdlMidiDevice *device, MMI32 volume)
{
    enter_control(device);
    device->volume = volume < 0 ? 0 : volume;
    mm_midi_render_volume(mm_midi_device_renderer_enter(device->renderer),
                          device->volume / (float)MM_SDL_MIDI_FULL_VOLUME);
    mm_midi_device_renderer_leave(device->renderer);
    leave_control(device);
}

MMI32 mm_sdl_midi_volume(MMSdlMidiDevice *device)
{
    MMI32 volume;
    enter_control(device);
    volume = device->volume;
    leave_control(device);
    return volume;
}

int mm_sdl_midi_queue(MMSdlMidiDevice *device, void *data, MMI32 bytes, MMI32 offset, MMI32 repeats, size_t tag)
{
    int ok;
    enter_control(device);
    ok = device->active && mm_midi_device_renderer_queue(device->renderer, data, bytes, offset, repeats, tag);
    leave_control(device);
    return ok;
}

void mm_sdl_midi_stop(MMSdlMidiDevice *device)
{
    enter_control(device);
    mm_midi_render_stop(mm_midi_device_renderer_enter(device->renderer));
    mm_midi_device_renderer_leave(device->renderer);
    /* SDL takes its stream lock before rendering. Drop the renderer lock
     * before taking the stream lock to discard pending PCM. */
    mm_sdl_audio_output_clear(device->output);
    mm_midi_worker_signal(device->worker);
    leave_control(device);
}

void mm_sdl_midi_stats(MMSdlMidiDevice *device, MMSdlMidiStats *stats)
{
    const MMMidiRenderer *renderer;
    enter_control(device);
    renderer = mm_midi_device_renderer_enter(device->renderer);
    stats->active = device->active;
    stats->queued = renderer->queued;
    stats->completed = renderer->completed;
    stats->messages = renderer->messages;
    stats->notes = renderer->notes;
    stats->frames = renderer->frames;
    stats->audible_frames = renderer->audible_frames;
    mm_midi_device_renderer_leave(device->renderer);
    leave_control(device);
}
