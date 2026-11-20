/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef SOUND_QUEUE_H
#define SOUND_QUEUE_H
#include "kauai_core.h"
#include "mm/frame_sound.h"
#include "mm/sound_resource.h"

enum
{
    styNil = 0,
    styUnused, // Existing content depends on subsequent values.
    stySfx,
    stySpeech,
    styMidi,
    styLim
};
const LegacyLong sclNonLoop = MM_SOUND_CLASS_NONLOOP;
const LegacyLong sclLoopWav = MM_SOUND_CLASS_LOOP_WAVE;
const LegacyLong sclLoopMidi = MM_SOUND_CLASS_LOOP_MIDI;
#define vlmNil (-1)
enum
{
    sqnActr = 0x10000000,
    sqnBkgd = 0x20000000,
    sqnLim
};
const LegacyLong ksqnStyShift = 16;
const LegacyLong kdtimOffMsq = MM_FRAME_SOUND_OFF;
const LegacyLong kdtimLongMsq = MM_FRAME_SOUND_LONG;
const LegacyLong kdtim2Msq = MM_FRAME_SOUND_SHORT;
#endif
