/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "audio_mixer.h"
#include <mutex>
namespace KauaiAudio
{
namespace
{
MMAudioCounter Counter(std::uint64_t n)
{
    return mm_audio_counter(std::uint32_t(n), std::uint32_t(n >> 32));
}
std::uint64_t Number(MMAudioCounter n)
{
    return (std::uint64_t(n.high) << 32) | n.low;
}
} // namespace
struct Mixer::Impl
{
    mutable std::recursive_mutex mutex;
    MMAudioMixer state;
    Impl()
    {
        const MMAudioSampleServices samples = {mm_audio_sample_retain, mm_audio_sample_release};
        mm_audio_mixer_init_samples(&state, nullptr, &samples);
    }
    ~Impl()
    {
        mm_audio_mixer_dispose(&state);
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
    return Number(mm_audio_mixer_add(&impl->state));
}
void Mixer::RemoveQueue(std::uint64_t id)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    mm_audio_mixer_remove(&impl->state, Counter(id));
}
bool Mixer::Sync(std::uint64_t id, const MMAudioVoice *voices, std::size_t count)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    return mm_audio_mixer_sync(&impl->state, Counter(id), voices, count) != 0;
}
bool Mixer::Sync(std::uint64_t id, const std::vector<MMAudioVoice> &voices)
{
    return Sync(id, voices.data(), voices.size());
}
bool Mixer::Done(std::uint64_t id, std::int32_t voice) const
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    return mm_audio_mixer_done(&impl->state, Counter(id), voice) != 0;
}
void Mixer::SetVolume(float v)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    mm_audio_mixer_volume(&impl->state, v);
}
void Mixer::Suspend(bool b)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    mm_audio_mixer_suspend(&impl->state, b);
}
void Mixer::BeginGroup()
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    mm_audio_mixer_begin(&impl->state);
}
void Mixer::EndGroup()
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    mm_audio_mixer_end(&impl->state);
}
void Mixer::Render(float *out, std::size_t n)
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    mm_audio_mixer_render(&impl->state, out, n);
}
MixerStats Mixer::Stats() const
{
    std::lock_guard<std::recursive_mutex> lock(impl->mutex);
    return {Number(impl->state.started), Number(impl->state.completed), Number(impl->state.rendered_frames)};
}
} // namespace KauaiAudio
