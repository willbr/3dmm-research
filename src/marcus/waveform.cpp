#include "waveform.h"
#include "sdl_audio.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
constexpr std::size_t bucketFrames = 128;
constexpr std::size_t cacheBudget = 32u * 1024 * 1024;
MarcusWaveformColumn Merge(MarcusWaveformColumn a, MarcusWaveformColumn b)
{
    return {std::min(a.minimum, b.minimum), std::max(a.maximum, b.maximum)};
}
}
void MarcusWaveformEnvelope::Build(const float *stereo, std::size_t frameCount)
{
    frames = frameCount;
    leaves = 1;
    const auto buckets = (frames + bucketFrames - 1) / bucketFrames;
    while (leaves < buckets)
        leaves *= 2;
    tree.assign(leaves * 2, {});
    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        auto &peak = tree[leaves + frame / bucketFrames];
        for (int channel = 0; channel < 2; ++channel)
        {
            const float value = stereo[frame * 2 + channel];
            if (std::isfinite(value))
            {
                peak.minimum = std::min(peak.minimum, value);
                peak.maximum = std::max(peak.maximum, value);
            }
        }
    }
    for (auto node = leaves; --node;)
        tree[node] = Merge(tree[node * 2], tree[node * 2 + 1]);
}
double MarcusWaveformEnvelope::Seconds() const
{
    return double(frames) / KauaiAudio::SampleRate;
}
std::size_t MarcusWaveformEnvelope::Bytes() const
{
    return tree.capacity() * sizeof(MarcusWaveformColumn);
}
void MarcusWaveformEnvelope::Read(double first, double step, int count, MarcusWaveformColumn *columns) const
{
    std::fill_n(columns, count, MarcusWaveformColumn{});
    if (!frames)
        return;
    for (int column = 0; column < count; ++column)
    {
        const double start = first + column * step;
        if (start >= Seconds())
            break;
        const double end = std::min(Seconds(), start + step);
        auto left = leaves + static_cast<std::size_t>(std::floor(start * KauaiAudio::SampleRate / bucketFrames));
        auto right = leaves + static_cast<std::size_t>(std::ceil(end * KauaiAudio::SampleRate / bucketFrames));
        right = std::min(right, leaves * 2);
        while (left < right)
        {
            if (left & 1)
                columns[column] = Merge(columns[column], tree[left++]);
            if (right & 1)
                columns[column] = Merge(columns[column], tree[--right]);
            left /= 2;
            right /= 2;
        }
    }
}
void MarcusWaveformCache::Clear()
{
    entries.clear();
    bytes = 0;
    clock = 0;
}
void MarcusWaveformCache::Read(ThreeMM::Preview &preview, const ThreeMM::PreviewSoundCue &cue, double first,
                               double step, int count, MarcusWaveformColumn *columns, MarcusWaveformInfo &info)
{
    // Scene-local cache: imported sound child remapping cannot leak across cuts.
    const auto key = std::make_tuple(cue.source, cue.sound.type, cue.sound.number, cue.child);
    // An explicit stop may carry the same identity as its earlier playable cue.
    if (cue.silent)
    {
        std::fill_n(columns, count, MarcusWaveformColumn{});
        info = {};
        info.kind = MARCUS_WAVEFORM_SILENT;
        std::snprintf(info.message, sizeof(info.message), "Stop / silence");
        return;
    }
    auto found = entries.find(key);
    if (found == entries.end())
    {
        Entry entry;
        ThreeMM::PreviewAudio audio;
        ThreeMM::Error error;
        entry.info.kind = MARCUS_WAVEFORM_UNAVAILABLE;
        if (!preview.ReadSound(cue, audio, error))
            std::snprintf(entry.info.message, sizeof(entry.info.message), "%s", error.message.c_str());
        else if (audio.silent)
        {
            entry.info.kind = MARCUS_WAVEFORM_SILENT;
            std::snprintf(entry.info.message, sizeof(entry.info.message), "Stop / silence");
        }
        else if (audio.midi)
        {
            entry.info.kind = MARCUS_WAVEFORM_MIDI;
            if (!audio.events.empty())
                entry.info.seconds = audio.events.back().milliseconds / 1000.0;
            std::snprintf(entry.info.message, sizeof(entry.info.message), "MIDI notes (no sampled waveform)");
        }
        else
        {
            std::shared_ptr<const KauaiAudio::Sample> sample;
            if (!KauaiAudio::DecodeWave(audio.wave.data(), audio.wave.size(), sample))
                std::snprintf(entry.info.message, sizeof(entry.info.message), "Cannot decode sound waveform.");
            else if (sample->stereo.size() > 256u * 1024 * 1024 / sizeof(float))
                std::snprintf(entry.info.message, sizeof(entry.info.message), "Sound exceeds the waveform decode budget.");
            else
            {
                entry.envelope.Build(sample->stereo.data(), sample->stereo.size() / 2);
                entry.info.seconds = entry.envelope.Seconds();
                entry.info.kind = MARCUS_WAVEFORM_SAMPLED;
            }
        }
        const auto required = entry.envelope.Bytes();
        while (!entries.empty() && (bytes + required > cacheBudget || entries.size() >= 512))
        {
            auto oldest = std::min_element(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
                return a.second.used < b.second.used;
            });
            bytes -= oldest->second.envelope.Bytes();
            entries.erase(oldest);
        }
        found = entries.emplace(key, std::move(entry)).first;
        bytes += required;
    }
    auto &entry = found->second;
    entry.used = ++clock;
    info = entry.info;
    entry.envelope.Read(first, step, count, columns);
}
