/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_PREFERENCES_H
#define STUDIO_PREFERENCES_H
#include "mm/playback.h"

// Process preferences only: these never become part of a movie or its assets.
struct StudioPreferences
{
    int playbackRate = 0;     // Original, 6, 12, 24 FPS, uncapped.
    int playbackModifier = 0; // Normal, Win98 -> XP, XP -> Win98.
    double PlaybackFps() const
    {
        return mm_playback_fps(playbackRate, playbackModifier);
    }
    bool allowMultipleInstances = false;
    bool backgroundPlayback = false;
    bool flatShading = false;
    bool skipStartupScreens = false;
    bool muteStartupSound = false;
    bool muteStudioSounds = false;
    bool cutTransitions = false;
    bool normalKeyRepeat = false;
    bool disableAudioFades = false;
};

inline StudioPreferences &CurrentStudioPreferences()
{
    static StudioPreferences preferences;
    return preferences;
}

#endif
