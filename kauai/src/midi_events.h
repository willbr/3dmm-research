/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_EVENTS_H
#define KAUAI_MIDI_EVENTS_H
#include <cstdint>
#include "three_mm/midi_render.h"
namespace KauaiMidi
{
// Fixed 12-byte records. A leading tempo of one second per quarter note and
// 1000 ticks per quarter makes the cached stream's deltaTicks milliseconds.
using StreamEvent = ThreeMMMidiStreamEvent;
constexpr std::uint32_t shortMessage = 0, tempo = 1, noOperation = 2;
} // namespace KauaiMidi
#endif
