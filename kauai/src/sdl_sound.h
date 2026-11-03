/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_SOUND_H
#define KAUAI_SDL_SOUND_H
#include <cstdint>
class SoundDevice;
namespace KauaiAudio
{
SoundDevice *NewWaveDevice();
struct StudioSoundStats
{
    bool active = false;
    std::uint64_t decoded = 0, started = 0, completed = 0;
};
StudioSoundStats StudioStats();
} // namespace KauaiAudio
#endif
