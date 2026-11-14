/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "audio_mixer.h"
#include "three_mm/audio_mixer.h"
#include <map>
#include <mutex>
#include <new>
namespace KauaiAudio
{
namespace
{
ThreeMMAudioCounter Counter(std::uint64_t n)
{
    return three_mm_audio_counter(std::uint32_t(n), std::uint32_t(n >> 32));
}
std::uint64_t Number(ThreeMMAudioCounter n)
{
    return (std::uint64_t(n.high) << 32) | n.low;
}
} // namespace
struct Mixer::Impl
{
    mutable std::recursive_mutex mutex;
    ThreeMMAudioMixer state;
    // Keep borrowed PCM alive. Playback state and queue ordering belong to C.
    std::map<std::uint64_t, std::vector<Voice>> samples;
    Impl()
    {
        three_mm_audio_mixer_init(&state, nullptr);
    }
    ~Impl()
    {
        three_mm_audio_mixer_dispose(&state);
    }
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
    const auto id = Number(three_mm_audio_mixer_add(&impl->state));
    if (!id)
        return 0;
    try
    {
        impl->samples.emplace(id, std::vector<Voice>{});
        return id;
    }
    catch (const std::bad_alloc &)
    {
        three_mm_audio_mixer_remove(&impl->state, Counter(id));
        return 0;
    }
}
void Mixer::RemoveQueue(std::uint64_t id)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    three_mm_audio_mixer_remove(&impl->state, Counter(id));
    impl->samples.erase(id);
}
bool Mixer::Sync(std::uint64_t id, const std::vector<Voice> &voices)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    auto found = impl->samples.find(id);
    if (found == impl->samples.end())
        return false;
    try
    {
        std::vector<Voice> retained(voices);
        std::vector<ThreeMMAudioVoice> input;
        input.reserve(voices.size());
        for (const auto &v : voices)
        {
            if (!v.sample || v.sample->stereo.size() % 2)
                return false;
            input.push_back({v.id, v.plays, v.sample.get(), v.sample->stereo.data(), v.sample->stereo.size() / 2,
                             v.volume, v.offsetMs, v.paused, v.stopped});
        }
        if (!three_mm_audio_mixer_sync(&impl->state, Counter(id), input.data(), input.size()))
            return false;
        found->second.swap(retained);
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
    return three_mm_audio_mixer_done(&impl->state, Counter(id), voice) != 0;
}
void Mixer::SetVolume(float v)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    three_mm_audio_mixer_volume(&impl->state, v);
}
void Mixer::Suspend(bool b)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    three_mm_audio_mixer_suspend(&impl->state, b);
}
void Mixer::BeginGroup()
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    three_mm_audio_mixer_begin(&impl->state);
}
void Mixer::EndGroup()
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    three_mm_audio_mixer_end(&impl->state);
}
void Mixer::Render(float *out, std::size_t n)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    three_mm_audio_mixer_render(&impl->state, out, n);
}
MixerStats Mixer::Stats() const
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    return {Number(impl->state.started), Number(impl->state.completed), Number(impl->state.rendered_frames)};
}
} // namespace KauaiAudio
