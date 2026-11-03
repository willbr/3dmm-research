/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_AUDIO_MIXER_H
#define KAUAI_AUDIO_MIXER_H
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
namespace KauaiAudio
{
constexpr int SampleRate = 48000;
struct Sample
{
    std::vector<float> stereo;
};
struct Voice
{
    std::int32_t id = 0;
    std::shared_ptr<const Sample> sample;
    float volume = 1;
    std::int32_t plays = 1; // Zero loops indefinitely.
    std::uint32_t offsetMs = 0;
    bool paused = false, stopped = false;
};
struct MixerStats
{
    std::uint64_t started = 0, completed = 0, renderedFrames = 0;
};
// Fixed-rate stereo mixer. Queues play sequentially and mix concurrently.
// Rendering performs no allocations and never calls the Kauai framework.
class Mixer
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Mixer();
    ~Mixer();
    std::uint64_t AddQueue();
    void RemoveQueue(std::uint64_t queue);
    bool Sync(std::uint64_t queue, const std::vector<Voice> &voices);
    bool Done(std::uint64_t queue, std::int32_t voice) const;
    void SetVolume(float volume);
    void Suspend(bool suspended);
    void BeginGroup();
    void EndGroup();
    void Render(float *stereo, std::size_t frames);
    MixerStats Stats() const;
    // Recursive main-thread transaction used by Kauai's queue adapter.
    void Lock();
    void Unlock();
};
} // namespace KauaiAudio
#endif
