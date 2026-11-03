/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_RENDER_H
#define KAUAI_MIDI_RENDER_H
#include "midi_events.h"
#include <cstddef>
#include <memory>
namespace KauaiMidi
{
struct RenderStats
{
    std::uint64_t queued = 0, completed = 0, messages = 0, notes = 0, frames = 0, audibleFrames = 0;
};
struct ReturnedBuffer
{
    void *data = nullptr;
    std::uintptr_t tag = 0;
};
// Frame-clock sequencer. Render never allocates or calls client code. Producer
// methods and returned-buffer draining synchronize with Render internally.
class Renderer
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Renderer();
    ~Renderer();
    bool Load(const void *bank, std::size_t bytes, int sampleRate);
    bool Queue(void *data, std::int32_t bytes, std::int32_t offset, std::int32_t repeats, std::uintptr_t tag);
    bool Render(float *stereo, std::size_t frames); // True if buffers await return.
    void Stop();
    bool Reset();         // Fresh device state; requires no borrowed buffers awaiting return.
    void SilenceIfIdle(); // Call after delivering completions, which may queue more data.
    bool PopReturned(ReturnedBuffer &buffer);
    bool Pending() const;
    void SetVolume(float volume);
    RenderStats Stats() const;
};
} // namespace KauaiMidi
#endif
