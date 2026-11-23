/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_SYNTH_H
#define KAUAI_MIDI_SYNTH_H
#include "mm/types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct tsf MMMidiSynth;
/* The caller serializes each synth. Load borrows the input while validating
 * all 128 bank-zero programs and the standard percussion kit. */
MMMidiSynth *mm_midi_synth_load(const void *data, size_t bytes, int rate);
void mm_midi_synth_destroy(void *);
void mm_midi_synth_message(void *, MMU32);
void mm_midi_synth_reset(void *);
/* Stop notes and reset pitch, preserving programs and controllers. */
void mm_midi_synth_silence(void *);
void mm_midi_synth_render(void *, float *stereo, size_t frames);
#ifdef __cplusplus
}
#endif
#endif
