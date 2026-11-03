/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_SYNTH_H
#define KAUAI_MIDI_SYNTH_H
#include <cstddef>
#include <cstdint>
#include <memory>
namespace KauaiMidi
{
// One renderer thread owns each synth. No native devices or framework objects.
class Synth
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Synth();
    ~Synth();
    // Requires all 128 bank-zero GM programs and the standard percussion kit.
    // Failure leaves an existing bank intact. Input is borrowed only while loading.
    bool Load(const void *data, std::size_t bytes, int sampleRate);
    bool Ready() const;
    void Message(std::uint32_t message);
    void Reset();
    void Silence(); // Stop notes/reset pitch; retain programs and controllers.
    void Render(float *stereo, std::size_t frames);
};
} // namespace KauaiMidi
#endif
