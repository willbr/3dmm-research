/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L /* TinySoundFont uses the float math interfaces. */
#endif
#include "midi_synth.h"
#include <string.h>
#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#include "tsf.h"

MMMidiSynth *mm_midi_synth_load(const void *data, size_t bytes, int rate)
{
    tsf *font;
    int i;
    if (!data || bytes < 12 || bytes > INT_MAX || rate < 8000 || rate > 192000)
        return NULL;
    font = tsf_load_memory(data, (int)bytes);
    if (!font)
        return NULL;
    for (i = 0; i < 128; ++i)
        if (tsf_get_presetindex(font, 0, i) < 0)
            goto fail;
    if (tsf_get_presetindex(font, 128, 0) < 0 || !tsf_set_max_voices(font, 256))
        goto fail;
    tsf_set_output(font, TSF_STEREO_INTERLEAVED, rate, -6.0f);
    for (i = 0; i < 16; ++i)
        if (!tsf_channel_set_presetnumber(font, i, 0, i == 9))
            goto fail;
    mm_midi_synth_reset(font);
    return font;
fail:
    tsf_close(font);
    return NULL;
}

void mm_midi_synth_destroy(void *synth)
{
    if (synth)
        tsf_close((tsf *)synth);
}

void mm_midi_synth_reset(void *synth)
{
    tsf *font = (tsf *)synth;
    int channel;
    if (!font)
        return;
    mm_midi_synth_silence(font);
    for (channel = 0; channel < 16; ++channel)
    {
        tsf_channel_set_sustain(font, channel, 0);
        tsf_channel_midi_control(font, channel, 121, 0);
        tsf_channel_set_pitchwheel(font, channel, 8192);
        tsf_channel_set_presetnumber(font, channel, 0, channel == 9);
    }
}

void mm_midi_synth_silence(void *synth)
{
    tsf *font = (tsf *)synth;
    int channel;
    if (!font)
        return;
    for (channel = 0; channel < 16; ++channel)
    {
        tsf_channel_sounds_off_all(font, channel);
        tsf_channel_set_sustain(font, channel, 0);
        tsf_channel_set_pitchwheel(font, channel, 8192);
    }
}

void mm_midi_synth_message(void *synth, MMU32 message)
{
    tsf *font = (tsf *)synth;
    const int channel = message & 15, first = (message >> 8) & 127, second = (message >> 16) & 127;
    if (!font)
        return;
    switch (message & 0xF0)
    {
    case 0x80:
        tsf_channel_note_off(font, channel, first);
        break;
    case 0x90:
        if (second)
            tsf_channel_note_on(font, channel, first, second / 127.0f);
        else
            tsf_channel_note_off(font, channel, first);
        break;
    case 0xB0:
        tsf_channel_midi_control(font, channel, first, second);
        break;
    case 0xC0:
        tsf_channel_set_presetnumber(font, channel, first, channel == 9);
        break;
    case 0xE0:
        tsf_channel_set_pitchwheel(font, channel, first | (second << 7));
        break;
    default:
        if ((message & 255) == 255)
            mm_midi_synth_reset(font);
        break;
    }
}

void mm_midi_synth_render(void *synth, float *stereo, size_t frames)
{
    tsf *font = (tsf *)synth;
    while (frames)
    {
        const int count = frames > 4096 ? 4096 : (int)frames;
        if (font)
            tsf_render_float(font, stereo, count, 0);
        else
            memset(stereo, 0, count * 2 * sizeof(float));
        stereo += count * 2;
        frames -= count;
    }
}
