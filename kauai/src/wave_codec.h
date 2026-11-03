/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_WAVE_CODEC_H
#define KAUAI_WAVE_CODEC_H
#include <cstdint>
#include <cstddef>
#include <vector>
namespace KauaiAudio
{
// RIFF writers take unsigned mono PCM bytes. Results are transactional and
// use fixed little-endian fields, independent of native SDK structure layout.
bool Pcm8Wave(const std::vector<std::uint8_t> &samples, std::uint32_t rate, std::vector<std::uint8_t> &wave);
bool PcmWave(const std::vector<std::uint8_t> &samples, std::uint32_t rate, unsigned channels, unsigned bits,
             std::vector<std::uint8_t> &wave);
bool AdpcmWave(const std::vector<std::uint8_t> &samples, std::uint32_t rate, std::vector<std::uint8_t> &wave);
// Normalized Studio imports: retain PCM for short clips affected by AudioMan's
// small ADPCM stream length bug; otherwise choose the smaller legacy encoding.
bool LegacyImportWave(const std::vector<std::uint8_t> &samples, std::vector<std::uint8_t> &wave);
bool LegacyWaveFormat(const void *data, std::size_t bytes);
// Valid nonzero fact length for compressed RIFF WAVE. PCM fact chunks and
// zero lengths are ignored for compatibility with existing files.
bool WaveFactFrames(const void *data, std::size_t bytes, std::uint32_t &frames);
} // namespace KauaiAudio
#endif
