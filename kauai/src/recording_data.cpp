/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "recording_data.h"
#include "wave_codec.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <new>
namespace KauaiAudio
{
bool RecordingBuffer::Init(unsigned sampleRate, unsigned channelCount, unsigned sampleBits, std::uint32_t maxMs)
{
    if (!sampleRate || sampleRate > 192000 || !channelCount || channelCount > 2 ||
        (sampleBits != 8 && sampleBits != 16) || !maxMs)
        return false;
    const auto frames = std::uint64_t(sampleRate) * maxMs / 1000;
    const auto bytes = frames * channelCount * (sampleBits / 8);
    if (!frames || bytes > INT32_MAX - 45)
        return false;
    try
    {
        std::vector<std::uint8_t> allocated(static_cast<std::size_t>(bytes));
        data.swap(allocated);
        rate = sampleRate;
        channels = channelCount;
        bits = sampleBits;
        Reset();
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
void RecordingBuffer::Reset()
{
    used = 0;
}
std::size_t RecordingBuffer::Append(const void *samples, std::size_t bytes)
{
    const auto frameBytes = channels * (bits / 8);
    if (!samples || !frameBytes || bytes % frameBytes)
        return 0;
    const auto count = std::min(bytes, data.size() - used);
    if (count)
        std::memcpy(data.data() + used, samples, count);
    used += count;
    return count;
}
bool RecordingBuffer::Full() const
{
    return !data.empty() && used == data.size();
}
std::size_t RecordingBuffer::Frames() const
{
    return channels ? used / (channels * (bits / 8)) : 0;
}
bool RecordingBuffer::Wave(std::vector<std::uint8_t> &wave) const
{
    if (Frames() <= 8)
        return false;
    try
    {
        const auto frameBytes = channels * (bits / 8);
        const auto frames = Frames() - 8;
        std::vector<std::uint8_t> pcm(data.begin(), data.begin() + frames * frameBytes);
        const auto read = [&](std::size_t sample) {
            if (bits == 8)
                return int(pcm[sample]) - 128;
            const auto at = sample * 2;
            const unsigned value = pcm[at] | (unsigned(pcm[at + 1]) << 8);
            return value >= 32768 ? int(value) - 65536 : int(value);
        };
        const auto biasFrames = std::min<std::size_t>(frames, std::max(1u, rate / 4));
        std::int64_t sum = 0;
        for (std::size_t i = 0; i < biasFrames * channels; ++i)
            sum += read(i);
        int bias = static_cast<int>(sum / static_cast<std::int64_t>(biasFrames * channels));
        if (std::abs(bias) < (bits == 8 ? 3 : 1000))
            bias = 0;
        // AudioMan's bias subtraction wraps in the original sample width.
        if (bias)
            for (std::size_t i = 0; i < frames * channels; ++i)
            {
                const int value = read(i) - bias;
                if (bits == 8)
                    pcm[i] = static_cast<std::uint8_t>(value + 128);
                else
                {
                    const auto word = static_cast<std::uint16_t>(value);
                    pcm[i * 2] = word & 255;
                    pcm[i * 2 + 1] = word >> 8;
                }
            }
        std::size_t first = frames, last = 0;
        for (std::size_t frame = 0; frame < frames; ++frame)
            for (unsigned channel = 0; channel < channels; ++channel)
                if (std::abs(read(frame * channels + channel)) >= (bits == 8 ? 3 : 823))
                {
                    first = std::min(first, frame);
                    last = frame + 1;
                }
        // Silence is retained, matching the original recorder. The gate is
        // used to find trim bounds only; it does not mute quiet interior audio.
        if (first != frames)
        {
            const auto margin = rate / 4;
            // Preserve AudioMan's one-based first-active position, including
            // its behavior when that position falls inside the lead margin.
            ++first;
            if (first > margin)
                first -= margin;
            last = std::min(frames, last + 2 * margin);
            if (first < last && (first || last != frames))
                pcm = std::vector<std::uint8_t>(pcm.begin() + first * frameBytes, pcm.begin() + last * frameBytes);
        }
        return PcmWave(pcm, rate, channels, bits, wave);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
} // namespace KauaiAudio
