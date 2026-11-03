/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_LEGACY_WAVE_FORMATS_H
#define KAUAI_LEGACY_WAVE_FORMATS_H
// Shared startup format IDs; these do not require the AudioMan device adapter.
// allowable formats for the audioman device
// WARNING: code assumes that (kwav*16 - kwav*8) is constant!
enum
{
    kwav11M8,
    kwav22M8,
    kwav44M8,
    kwav11S8,
    kwav22S8,
    kwav44S8,
    kwav11M16,
    kwav22M16,
    kwav44M16,
    kwav11S16,
    kwav22S16,
    kwav44S16,
    kwavLim
};

#endif
