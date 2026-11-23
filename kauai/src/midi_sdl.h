/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_SDL_H
#define KAUAI_MIDI_SDL_H
#include "midi_render.h"
#include "midi_stream_host.h"
namespace KauaiMidi
{
StreamHost *CreateSDLStreamHost(Completion completion, std::uintptr_t user);
struct RenderStats
{
    std::uint64_t queued = 0, completed = 0, messages = 0, notes = 0, frames = 0, audibleFrames = 0;
};
struct SDLStats
{
    bool attempted = false, loaded = false, active = false;
    RenderStats rendering;
};
SDLStats StudioMidiStats();
} // namespace KauaiMidi
#endif
