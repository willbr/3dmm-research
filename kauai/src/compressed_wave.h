/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_COMPRESSED_WAVE_H
#define KAUAI_COMPRESSED_WAVE_H
#include <cstddef>
#include <cstdint>
#include <vector>
namespace KauaiAudio
{
struct DecodedWave
{
    int sampleRate = 0, channels = 0;
    std::vector<std::int16_t> samples;
};
enum class CompressedWaveResult
{
    NotHandled,
    Invalid,
    Decoded
};
// Decode GSM 6.10 (Microsoft WAV49) and MP3-in-WAV without native codecs.
// Other WAVE encodings remain SDL's responsibility. Output changes only on success.
CompressedWaveResult DecodeCompressedWave(const void *data, std::size_t bytes, DecodedWave &decoded);
const char *CompressedWaveLicenseText();
} // namespace KauaiAudio
#endif
