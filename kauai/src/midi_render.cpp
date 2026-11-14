/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_render.h"
#include "midi_synth.h"
#include "three_mm/midi_render.h"
#include <mutex>
namespace KauaiMidi
{
namespace
{
std::uint64_t Number(ThreeMMAudioCounter n)
{
    return (std::uint64_t(n.high) << 32) | n.low;
}
} // namespace
struct Renderer::Impl
{
    mutable std::mutex mutex;
    Synth synth;
    ThreeMMMidiRenderer state;
    Impl()
    {
        ThreeMMMidiRenderServices s = {};
        s.context = &synth;
        s.ready = [](void *p) -> int { return static_cast<Synth *>(p)->Ready(); };
        s.message = [](void *p, ThreeMMU32 m) { static_cast<Synth *>(p)->Message(m); };
        s.render = [](void *p, float *out, std::size_t n) { static_cast<Synth *>(p)->Render(out, n); };
        s.silence = [](void *p) { static_cast<Synth *>(p)->Silence(); };
        s.reset = [](void *p) { static_cast<Synth *>(p)->Reset(); };
        three_mm_midi_render_init(&state, &s, nullptr);
    }
    ~Impl()
    {
        three_mm_midi_render_dispose(&state);
    }
};
Renderer::Renderer() : impl(new Impl)
{
}
Renderer::~Renderer() = default;
bool Renderer::Load(const void *bank, std::size_t bytes, int rate)
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->state.head || impl->state.returned || !impl->synth.Load(bank, bytes, rate))
        return false;
    three_mm_midi_render_clock_reset(&impl->state, static_cast<std::uint32_t>(rate));
    return true;
}
bool Renderer::Queue(void *data, std::int32_t bytes, std::int32_t offset, std::int32_t repeats, std::uintptr_t tag)
{
    if (reinterpret_cast<std::uintptr_t>(data) % alignof(StreamEvent))
        return false;
    std::lock_guard<std::mutex> lock(impl->mutex);
    return three_mm_midi_render_queue(&impl->state, static_cast<StreamEvent *>(data), bytes, offset, repeats, tag) != 0;
}
bool Renderer::Render(float *out, std::size_t n)
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return three_mm_midi_render(&impl->state, out, n) != 0;
}
void Renderer::Stop()
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    three_mm_midi_render_stop(&impl->state);
}
bool Renderer::PopReturned(ReturnedBuffer &b)
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    std::size_t tag;
    if (!three_mm_midi_render_pop(&impl->state, &b.data, &tag))
        return false;
    b.tag = tag;
    return true;
}
void Renderer::SilenceIfIdle()
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    three_mm_midi_render_idle(&impl->state);
}
bool Renderer::Reset()
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return three_mm_midi_render_reset(&impl->state) != 0;
}
bool Renderer::Pending() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->state.head != nullptr;
}
void Renderer::SetVolume(float v)
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    three_mm_midi_render_volume(&impl->state, v);
}
RenderStats Renderer::Stats() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto &s = impl->state;
    return {Number(s.queued), Number(s.completed), Number(s.messages),
            Number(s.notes),  Number(s.frames),    Number(s.audible_frames)};
}
} // namespace KauaiMidi
