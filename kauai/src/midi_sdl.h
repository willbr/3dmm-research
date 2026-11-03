/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_SDL_H
#define KAUAI_MIDI_SDL_H
#include "midi_render.h"
#include "midi_stream_host.h"
namespace KauaiMidi
{
StreamHost *CreateSDLStreamHost(Completion completion, std::uintptr_t user);
struct SDLStats
{
    bool attempted = false, loaded = false, active = false;
    RenderStats rendering;
};
SDLStats StudioMidiStats();
} // namespace KauaiMidi
#endif
