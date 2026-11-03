/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_synth.h"
#include <algorithm>
#include <climits>
#include <cmath>
#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#include "tsf.h"
namespace KauaiMidi
{
struct Synth::Impl
{
    tsf *font = nullptr;
    ~Impl()
    {
        if (font)
            tsf_close(font);
    }
};
Synth::Synth() : impl(new Impl)
{
}
Synth::~Synth() = default;
bool Synth::Load(const void *data, std::size_t bytes, int sampleRate)
{
    if (!data || bytes < 12 || bytes > INT_MAX || sampleRate < 8000 || sampleRate > 192000)
        return false;
    std::unique_ptr<tsf, decltype(&tsf_close)> font(tsf_load_memory(data, static_cast<int>(bytes)), tsf_close);
    if (!font)
        return false;
    for (int program = 0; program < 128; ++program)
        if (tsf_get_presetindex(font.get(), 0, program) < 0)
            return false;
    if (tsf_get_presetindex(font.get(), 128, 0) < 0 || !tsf_set_max_voices(font.get(), 256))
        return false;
    tsf_set_output(font.get(), TSF_STEREO_INTERLEAVED, sampleRate, -6.0f);
    for (int channel = 0; channel < 16; ++channel)
        if (!tsf_channel_set_presetnumber(font.get(), channel, 0, channel == 9))
            return false;
    if (impl->font)
        tsf_close(impl->font);
    impl->font = font.release();
    Reset();
    return true;
}
bool Synth::Ready() const
{
    return impl->font != nullptr;
}
void Synth::Reset()
{
    auto font = impl->font;
    if (!font)
        return;
    Silence();
    for (int channel = 0; channel < 16; ++channel)
    {
        tsf_channel_set_sustain(font, channel, 0);
        tsf_channel_midi_control(font, channel, 121, 0);
        tsf_channel_set_pitchwheel(font, channel, 8192);
        tsf_channel_set_presetnumber(font, channel, 0, channel == 9);
    }
}
void Synth::Silence()
{
    auto font = impl->font;
    if (!font)
        return;
    for (int channel = 0; channel < 16; ++channel)
    {
        tsf_channel_sounds_off_all(font, channel);
        tsf_channel_set_sustain(font, channel, 0);
        tsf_channel_set_pitchwheel(font, channel, 8192);
    }
}
void Synth::Message(std::uint32_t message)
{
    auto font = impl->font;
    if (!font)
        return;
    const int channel = message & 15, first = (message >> 8) & 127, second = (message >> 16) & 127;
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
            Reset();
        break;
    }
}
void Synth::Render(float *stereo, std::size_t frames)
{
    while (frames)
    {
        const auto count = static_cast<int>(std::min<std::size_t>(frames, 4096));
        if (impl->font)
            tsf_render_float(impl->font, stereo, count, 0);
        else
            std::fill_n(stereo, count * 2, 0.0f);
        stereo += count * 2;
        frames -= count;
    }
}
} // namespace KauaiMidi
