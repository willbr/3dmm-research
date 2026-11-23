/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_RENDER_H
#define KAUAI_MIDI_RENDER_H
#include "mm/midi_render.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMMidiDeviceRenderer MMMidiDeviceRenderer;
MMMidiDeviceRenderer *mm_midi_device_renderer_create(void);
/* Stop the device and its completion worker before destroying the renderer. */
void mm_midi_device_renderer_destroy(MMMidiDeviceRenderer *);
/* Borrow the sequencer under its native recursive lock. Rendering never
 * allocates or invokes a client completion. Deliver returns after leave. */
MMMidiRenderer *mm_midi_device_renderer_enter(MMMidiDeviceRenderer *);
void mm_midi_device_renderer_leave(MMMidiDeviceRenderer *);
/* Requires no pending or returned buffers. Failure preserves the bank and clock. */
int mm_midi_device_renderer_load(MMMidiDeviceRenderer *, const void *bank, size_t bytes, int rate);
int mm_midi_device_renderer_queue(MMMidiDeviceRenderer *, void *data, MMI32 bytes,
                                  MMI32 offset, MMI32 repeats, size_t tag);
#ifdef __cplusplus
}
#endif
#endif
