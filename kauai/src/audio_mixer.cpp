/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "audio_mixer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <new>
namespace KauaiAudio
{
struct Mixer::Impl
{
    struct Playing
    {
        Voice voice;
        std::uint64_t position = 0;
        bool done = false, started = false, grouped = false;
    };
    struct Queue
    {
        std::vector<Playing> voices;
        std::size_t head = 0;
    };
    mutable std::recursive_mutex mutex;
    std::map<std::uint64_t, Queue> queues;
    std::uint64_t next = 1;
    float volume = 1;
    bool suspended = false;
    unsigned groups = 0;
    MixerStats stats;
};
Mixer::Mixer() : impl(new Impl)
{
}
Mixer::~Mixer() = default;
void Mixer::Lock()
{
    impl->mutex.lock();
}
void Mixer::Unlock()
{
    impl->mutex.unlock();
}
std::uint64_t Mixer::AddQueue()
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    if (!impl->next)
        return 0;
    try
    {
        const auto id = impl->next++;
        impl->queues.emplace(id, Impl::Queue{});
        return id;
    }
    catch (const std::bad_alloc &)
    {
        return 0;
    }
}
void Mixer::RemoveQueue(std::uint64_t id)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    impl->queues.erase(id);
}
bool Mixer::Sync(std::uint64_t id, const std::vector<Voice> &voices)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    auto found = impl->queues.find(id);
    if (found == impl->queues.end())
        return false;
    try
    {
        std::vector<Impl::Playing> result;
        result.reserve(voices.size());
        for (const auto &voice : voices)
        {
            if (!voice.id || voice.plays < 0 || !voice.sample || voice.sample->stereo.size() % 2 ||
                !std::isfinite(voice.volume) || voice.volume < 0)
                return false;
            for (const auto &earlier : result)
                if (earlier.voice.id == voice.id)
                    return false;
            Impl::Playing playing;
            const auto old = std::find_if(found->second.voices.begin(), found->second.voices.end(),
                                          [&](const Impl::Playing &item) { return item.voice.id == voice.id; });
            if (old != found->second.voices.end())
            {
                if (old->voice.sample != voice.sample || old->voice.plays != voice.plays ||
                    old->voice.offsetMs != voice.offsetMs)
                    return false;
                playing = *old;
            }
            else
            {
                playing.position = std::uint64_t(voice.offsetMs) * SampleRate / 1000;
                playing.grouped = impl->groups > 0;
            }
            playing.voice = voice;
            result.push_back(std::move(playing));
        }
        found->second.voices.swap(result);
        found->second.head = 0;
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool Mixer::Done(std::uint64_t id, std::int32_t voice) const
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    auto queue = impl->queues.find(id);
    if (queue == impl->queues.end())
        return true;
    for (const auto &item : queue->second.voices)
        if (item.voice.id == voice)
            return item.done || item.voice.stopped;
    return false; // Not submitted yet.
}
void Mixer::SetVolume(float volume)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    if (std::isfinite(volume) && volume >= 0)
        impl->volume = volume;
}
void Mixer::Suspend(bool suspended)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    impl->suspended = suspended;
}
void Mixer::BeginGroup()
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    ++impl->groups;
}
void Mixer::EndGroup()
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    if (impl->groups && --impl->groups == 0)
        for (auto &queue : impl->queues)
            for (auto &item : queue.second.voices)
                item.grouped = false;
}
void Mixer::Render(float *output, std::size_t frames)
{
    if (!output || frames > SIZE_MAX / (2 * sizeof(float)))
        return;
    std::fill(output, output + frames * 2, 0.0f);
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    if (impl->suspended)
        return;
    for (auto &entry : impl->queues)
    {
        auto &queue = entry.second;
        std::size_t offset = 0;
        while (offset < frames && queue.head < queue.voices.size())
        {
            auto &item = queue.voices[queue.head];
            const auto count = item.voice.sample->stereo.size() / 2;
            if (item.done || item.voice.stopped || !count ||
                (item.voice.plays && item.position / count >= static_cast<std::uint32_t>(item.voice.plays)))
            {
                if (!item.done)
                {
                    item.done = true;
                    ++impl->stats.completed;
                }
                ++queue.head;
                continue;
            }
            if (item.voice.paused || item.grouped)
                break;
            if (!item.started)
            {
                item.started = true;
                ++impl->stats.started;
            }
            const auto position = static_cast<std::size_t>(item.position % count);
            const auto take = std::min(frames - offset, count - position);
            const auto volume = item.voice.volume * impl->volume;
            for (std::size_t i = 0; i < take * 2; ++i)
                output[offset * 2 + i] += item.voice.sample->stereo[position * 2 + i] * volume;
            item.position += take;
            offset += take;
            if (item.voice.plays && item.position / count >= static_cast<std::uint32_t>(item.voice.plays))
            {
                item.done = true;
                ++impl->stats.completed;
                ++queue.head;
            }
        }
    }
    for (std::size_t i = 0; i < frames * 2; ++i)
        output[i] = std::max(-1.0f, std::min(1.0f, output[i]));
    impl->stats.renderedFrames += frames;
}
MixerStats Mixer::Stats() const
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    return impl->stats;
}
} // namespace KauaiAudio
