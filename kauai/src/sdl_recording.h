/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_RECORDING_H
#define KAUAI_SDL_RECORDING_H
#include <cstdint>
#include <memory>
#include <vector>
namespace KauaiAudio
{
struct RecordingStats
{
    std::uint64_t opened = 0, started = 0, completed = 0, previewed = 0, capturedFrames = 0;
};
RecordingStats RecorderStats();
// Main-thread controls; SDL serializes its capture callback with stream locks.
class Recorder
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Recorder();
    ~Recorder();
    bool Open(unsigned rate, unsigned channels, unsigned bits, std::uint32_t maxMs);
    bool Start();
    bool Stop();
    bool Recording();
    bool Play(float volume = 1);
    void SuspendPreview(bool suspended);
    bool Playing();
    bool HaveSound() const;
    const std::vector<std::uint8_t> &Wave() const;
};
} // namespace KauaiAudio
#endif
