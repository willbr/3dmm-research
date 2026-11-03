/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_AUDIO_H
#define KAUAI_SDL_AUDIO_H
#include "audio_mixer.h"
namespace KauaiAudio
{
bool DecodeWave(const void *data, std::size_t bytes, std::shared_ptr<const Sample> &sample);
// Preserve compatible source bytes when compression would not save space;
// otherwise use legacy 11.025 kHz mono PCM for short clips or MS ADPCM.
bool PrepareWaveImport(const void *data, std::size_t bytes, std::vector<std::uint8_t> &wave);
class Output
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Output();
    ~Output();
    bool Open();
    Mixer &Engine();
};
} // namespace KauaiAudio
#endif
