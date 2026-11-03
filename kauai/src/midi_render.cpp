/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_render.h"
#include "midi_synth.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <new>
namespace KauaiMidi
{
struct Renderer::Impl
{
    struct Buffer
    {
        void *data;
        std::uintptr_t tag;
        std::size_t count, index;
        std::int32_t repeats;
        bool delayLoaded = false;
        Buffer *next = nullptr;
    };
    mutable std::mutex mutex;
    Synth synth;
    Buffer *head = nullptr, *tail = nullptr, *returned = nullptr, *returnedTail = nullptr;
    std::uint64_t waitFrames = 0, fraction = 0;
    std::uint32_t tempo = 1000000, rate = 48000;
    float volume = 1;
    RenderStats stats;
    void ReturnHead()
    {
        auto buffer = head;
        head = buffer->next;
        if (!head)
        {
            tail = nullptr;
        }
        buffer->next = nullptr;
        if (returnedTail)
            returnedTail->next = buffer;
        else
            returned = buffer;
        returnedTail = buffer;
        ++stats.completed;
    }
    std::uint64_t Delay(std::uint32_t ticks)
    {
        // Split the product to avoid overflow at the largest legal tick/tempo.
        const auto duration = std::uint64_t(ticks) * tempo;
        const auto remainder = (duration % 1000000000) * rate + fraction;
        fraction = remainder % 1000000000;
        return (duration / 1000000000) * rate + remainder / 1000000000;
    }
    ~Impl()
    {
        while (head)
            ReturnHead();
        while (returned)
        {
            auto next = returned->next;
            delete returned;
            returned = next;
        }
    }
};
Renderer::Renderer() : impl(new Impl)
{
}
Renderer::~Renderer() = default;
bool Renderer::Load(const void *bank, std::size_t bytes, int sampleRate)
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->head || impl->returned || !impl->synth.Load(bank, bytes, sampleRate))
        return false;
    impl->rate = static_cast<std::uint32_t>(sampleRate);
    impl->waitFrames = impl->fraction = 0;
    impl->tempo = 1000000;
    return true;
}
bool Renderer::Queue(void *data, std::int32_t bytes, std::int32_t offset, std::int32_t repeats, std::uintptr_t tag)
{
    if (!data || bytes <= 0 || offset < 0 || offset >= bytes || bytes % sizeof(StreamEvent) ||
        offset % sizeof(StreamEvent) || reinterpret_cast<std::uintptr_t>(data) % alignof(StreamEvent))
        return false;
    auto events = static_cast<const StreamEvent *>(data);
    const auto count = static_cast<std::size_t>(bytes) / sizeof(StreamEvent);
    bool timed = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto type = events[i].eventWord >> 24;
        if (events[i].streamId || type > noOperation || (type == tempo && !(events[i].eventWord & 0xFFFFFF)))
            return false;
        timed |= events[i].deltaTicks != 0;
    }
    if (repeats != 1 && !timed)
        return false; // An infinite zero-time loop cannot render.
    std::unique_ptr<Impl::Buffer> buffer(new (std::nothrow) Impl::Buffer{
        data, tag, count, static_cast<std::size_t>(offset) / sizeof(StreamEvent), repeats});
    if (!buffer)
        return false;
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->synth.Ready())
        return false;
    if (impl->tail)
        impl->tail->next = buffer.get();
    else
        impl->head = buffer.get();
    impl->tail = buffer.release();
    ++impl->stats.queued;
    return true;
}
bool Renderer::Render(float *stereo, std::size_t frames)
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!stereo && frames)
        return false;
    const auto requested = frames;
    for (;;)
    {
        while (impl->head && !impl->waitFrames)
        {
            auto &buffer = *impl->head;
            if (buffer.index == buffer.count)
            {
                if (buffer.repeats == 1)
                    impl->ReturnHead();
                else
                {
                    if (buffer.repeats > 0)
                        --buffer.repeats;
                    buffer.index = 0;
                    buffer.delayLoaded = false;
                }
                continue;
            }
            const auto &event = static_cast<const StreamEvent *>(buffer.data)[buffer.index];
            if (!buffer.delayLoaded)
            {
                impl->waitFrames = impl->Delay(event.deltaTicks);
                buffer.delayLoaded = true;
                if (impl->waitFrames)
                    break;
            }
            const auto type = event.eventWord >> 24;
            if (type == tempo)
                impl->tempo = event.eventWord & 0xFFFFFF;
            else if (type == shortMessage)
            {
                impl->synth.Message(event.eventWord);
                ++impl->stats.messages;
                if ((event.eventWord & 0xF0) == 0x90 && (event.eventWord & 0x7F0000))
                    ++impl->stats.notes;
            }
            ++buffer.index;
            buffer.delayLoaded = false;
        }
        if (!frames)
            break;
        const auto count =
            impl->waitFrames ? static_cast<std::size_t>(std::min<std::uint64_t>(impl->waitFrames, frames)) : frames;
        impl->synth.Render(stereo, count);
        for (std::size_t i = 0; i < count; ++i)
        {
            float &left = stereo[2 * i], &right = stereo[2 * i + 1];
            left = std::isfinite(left) ? std::max(-1.0f, std::min(1.0f, left * impl->volume)) : 0;
            right = std::isfinite(right) ? std::max(-1.0f, std::min(1.0f, right * impl->volume)) : 0;
            if (left != 0 || right != 0)
                ++impl->stats.audibleFrames;
        }
        frames -= count;
        stereo += 2 * count;
        if (impl->waitFrames)
            impl->waitFrames -= count;
    }
    impl->stats.frames += requested;
    return impl->returned != nullptr;
}
void Renderer::Stop()
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    while (impl->head)
        impl->ReturnHead();
    impl->synth.Silence();
    impl->waitFrames = impl->fraction = 0;
    impl->tempo = 1000000;
}
bool Renderer::PopReturned(ReturnedBuffer &buffer)
{
    Impl::Buffer *node;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        node = impl->returned;
        if (!node)
            return false;
        impl->returned = node->next;
        if (!impl->returned)
            impl->returnedTail = nullptr;
        buffer = {node->data, node->tag};
    }
    delete node;
    return true;
}
void Renderer::SilenceIfIdle()
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->head)
        impl->synth.Silence();
}
bool Renderer::Reset()
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->head || impl->returned)
        return false;
    impl->synth.Reset();
    impl->waitFrames = impl->fraction = 0;
    impl->tempo = 1000000;
    return true;
}
bool Renderer::Pending() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->head != nullptr;
}
void Renderer::SetVolume(float volume)
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->volume = std::isfinite(volume) ? std::max(0.0f, std::min(1.0f, volume)) : 0;
}
RenderStats Renderer::Stats() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->stats;
}
} // namespace KauaiMidi
