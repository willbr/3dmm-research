/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_PREFERENCES_H
#define STUDIO_PREFERENCES_H

// Process preferences only: these never become part of a movie or its assets.
struct StudioPreferences
{
    int playbackRate = 0;     // Original, 6, 12, 24 FPS, uncapped.
    int playbackModifier = 0; // Normal, Win98 -> XP, XP -> Win98.
    double PlaybackFps() const
    {
        if (playbackRate == 4)
            return 0;
        if (playbackRate == 0 && playbackModifier == 0)
            return -1; // Preserve the original scheduling path by default.
        const double fps = playbackRate == 2 ? 12 : playbackRate == 3 ? 24 : 6;
        return fps * (playbackModifier == 1 ? 1.35805 : playbackModifier == 2 ? 0.918485 : 1.0);
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
