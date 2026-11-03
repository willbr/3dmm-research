/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_WORKER_H
#define KAUAI_MIDI_WORKER_H
#include <cstdint>
namespace KauaiMidi
{
class Worker
{
    struct State;
    State *state = nullptr;

  public:
    using Work = std::uint32_t (*)(void *user);
    Worker() = default;
    ~Worker();
    Worker(const Worker &) = delete;
    Worker &operator=(const Worker &) = delete;
    // Starts once, initially waiting indefinitely. Work returns the next wait
    // in milliseconds (UINT32_MAX means indefinite). Signals coalesce and are
    // retained while Work runs. Start/Stop belong to the owner thread; Work
    // must not stop/destroy its own worker. Signal may race with Stop.
    bool Start(Work work, void *user);
    void Signal();
    bool Running() const;
    void Stop(); // Wakes and joins, including an in-flight Work invocation.
};
} // namespace KauaiMidi
#endif
