/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_STREAM_HOST_H
#define KAUAI_MIDI_STREAM_HOST_H
#include <cstdint>
#include "midi_events.h"
#include "kauai_core.h"
namespace KauaiMidi
{
using Completion = void (*)(std::uintptr_t user, void *buffer, std::uintptr_t tag);
class StreamHost;
using HostFactory = StreamHost *(*)(Completion completion, std::uintptr_t user);
class StreamHost : public BASE
{
  public:
    virtual void SetVlm(LegacyLong volume) = 0;
    virtual LegacyLong VlmCur() = 0;
    virtual bool FActive() = 0;
    virtual bool FActivate(bool active) = 0;
    // Buffers are borrowed until exactly one completion callback, including
    // stopped buffers. The offset applies to the first pass; repeats start at
    // the beginning. Completion may run on a worker thread outside host locks.
    virtual bool FQueueBuffer(void *buffer, LegacyLong bytes, LegacyLong offset, LegacyLong repeats,
                              std::uintptr_t tag) = 0;
    virtual void StopPlaying() = 0;
    virtual const char *ProviderName() const = 0;
};
// The platform supplies this factory. Release the returned host through BASE's
// reference-counted ownership after stopping/deactivating it and receiving all
// borrowed buffers back. No native device handles cross this boundary.
StreamHost *CreateStreamHost(Completion completion, std::uintptr_t user);
} // namespace KauaiMidi
#endif
