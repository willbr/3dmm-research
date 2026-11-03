/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstddef>
#include <type_traits>
#include "midi_stream_host.h"
#include "mididev2.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_INC_MMSYSTEM)
#error The MIDI stream host API must not import the Windows SDK
#endif
static_assert(sizeof(KauaiMidi::StreamEvent) == 12, "MIDI event layout changed");
static_assert(offsetof(KauaiMidi::StreamEvent, eventWord) == 8, "MIDI event word moved");
static_assert(sizeof(std::uintptr_t) == sizeof(void *), "MIDI callback values must hold pointers");
static_assert(std::is_abstract<KauaiMidi::StreamHost>::value, "Host contract must remain abstract");
