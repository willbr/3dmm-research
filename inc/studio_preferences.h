/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_PREFERENCES_H
#define STUDIO_PREFERENCES_H

// Process preferences only: these never become part of a movie or its assets.
struct StudioPreferences
{
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
