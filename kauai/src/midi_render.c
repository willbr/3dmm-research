/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_render.h"
#include "midi_synth.h"
#include "mm/native_runtime.h"
#include <stdlib.h>

struct MMMidiDeviceRenderer
{
    MMNativeMutex *mutex;
    MMMidiSynth *synth;
    MMMidiRenderer core;
};

static int ready(void *p) { return ((MMMidiDeviceRenderer *)p)->synth != NULL; }
static void message(void *p, MMU32 value) { mm_midi_synth_message(((MMMidiDeviceRenderer *)p)->synth, value); }
static void render(void *p, float *stereo, size_t frames)
{
    mm_midi_synth_render(((MMMidiDeviceRenderer *)p)->synth, stereo, frames);
}
static void silence(void *p) { mm_midi_synth_silence(((MMMidiDeviceRenderer *)p)->synth); }
static void reset(void *p) { mm_midi_synth_reset(((MMMidiDeviceRenderer *)p)->synth); }

MMMidiDeviceRenderer *mm_midi_device_renderer_create(void)
{
    MMMidiRenderServices services;
    MMMidiDeviceRenderer *renderer = (MMMidiDeviceRenderer *)calloc(1, sizeof(*renderer));
    if (!renderer)
        return NULL;
    renderer->mutex = mm_native_mutex_create(NULL);
    if (!renderer->mutex)
    {
        free(renderer);
        return NULL;
    }
    services.context = renderer;
    services.ready = ready;
    services.message = message;
    services.render = render;
    services.silence = silence;
    services.reset = reset;
    mm_midi_render_init(&renderer->core, &services, NULL);
    return renderer;
}

void mm_midi_device_renderer_destroy(MMMidiDeviceRenderer *renderer)
{
    if (!renderer)
        return;
    mm_midi_render_dispose(&renderer->core);
    mm_midi_synth_destroy(renderer->synth);
    mm_native_mutex_destroy(renderer->mutex);
    free(renderer);
}

MMMidiRenderer *mm_midi_device_renderer_enter(MMMidiDeviceRenderer *renderer)
{
    if (mm_native_mutex_enter(renderer->mutex) != 0)
        abort();
    return &renderer->core;
}

void mm_midi_device_renderer_leave(MMMidiDeviceRenderer *renderer)
{
    if (mm_native_mutex_leave(renderer->mutex) != 0)
        abort();
}

int mm_midi_device_renderer_load(MMMidiDeviceRenderer *renderer, const void *bank, size_t bytes, int rate)
{
    MMMidiRenderer *core = mm_midi_device_renderer_enter(renderer);
    MMMidiSynth *synth = NULL;
    if (!core->head && !core->returned)
        synth = mm_midi_synth_load(bank, bytes, rate);
    if (synth)
    {
        mm_midi_synth_destroy(renderer->synth);
        renderer->synth = synth;
        mm_midi_render_clock_reset(core, (MMU32)rate);
    }
    mm_midi_device_renderer_leave(renderer);
    return synth != NULL;
}

typedef struct MidiEventAlignment
{
    char byte;
    MMMidiStreamEvent event;
} MidiEventAlignment;

int mm_midi_device_renderer_queue(MMMidiDeviceRenderer *renderer, void *data, MMI32 bytes,
                                  MMI32 offset, MMI32 repeats, size_t tag)
{
    MMMidiRenderer *core;
    int result;
    if ((size_t)data % offsetof(MidiEventAlignment, event))
        return 0;
    core = mm_midi_device_renderer_enter(renderer);
    result = mm_midi_render_queue(core, (MMMidiStreamEvent *)data, bytes, offset, repeats, tag);
    mm_midi_device_renderer_leave(renderer);
    return result;
}
