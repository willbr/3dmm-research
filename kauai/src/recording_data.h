/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_RECORDING_DATA_H
#define KAUAI_RECORDING_DATA_H
#include <cstddef>
#include <cstdint>
#include <vector>
namespace KauaiAudio
{
// Bounded interleaved PCM storage; callers serialize capture and control access.
// Append performs no allocations. Samples are unsigned 8-bit or signed LE16.
class RecordingBuffer
{
    std::vector<std::uint8_t> data;
    std::size_t used = 0;
    unsigned rate = 0, channels = 0, bits = 0;

  public:
    bool Init(unsigned sampleRate, unsigned channelCount, unsigned sampleBits, std::uint32_t maxMs);
    void Reset();
    std::size_t Append(const void *samples, std::size_t bytes);
    bool Full() const;
    std::size_t Frames() const;
    // Remove the final eight frames, correct DC bias using the first 250 ms,
    // trim quiet ends with 250/500 ms margins, and write a legacy PCM WAVE.
    // An empty/too-short capture fails without modifying the result.
    bool Wave(std::vector<std::uint8_t> &wave) const;
};
} // namespace KauaiAudio
#endif
