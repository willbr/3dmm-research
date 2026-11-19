#include "sound_timeline.h"
#include <algorithm>
#include <cmath>
#include <istream>
#include <new>
#include <ostream>
#include <type_traits>

namespace
{
template <typename T> bool Put(std::ostream &out, const T &value)
{
    static_assert(std::is_trivially_copyable<T>::value, "IPC value must be flat");
    return bool(out.write(reinterpret_cast<const char *>(&value), sizeof(value)));
}
template <typename T> bool Get(std::istream &in, T &value)
{
    static_assert(std::is_trivially_copyable<T>::value, "IPC value must be flat");
    return bool(in.read(reinterpret_cast<char *>(&value), sizeof(value)));
}
constexpr std::uint32_t timingMagic = 0x33544d4d; // MMT3, matching-build ephemeral IPC.
} // namespace

bool MarcusSoundTimeline::Write(std::ostream &out) const
{
    const double frameSeconds = marcus_sound_schedule_frame_seconds(schedule.get());
    if (!Put(out, timingMagic) || !Put(out, frameSeconds) || !Put(out, std::uint32_t(sources.size()))) return false;
    for (const auto &source : sources)
    {
        if (!Put(out, source.info) || !Put(out, source.timing.transition) ||
            !Put(out, std::uint32_t(source.timing.scrolling.size()))) return false;
        for (const auto &scroll : source.timing.scrolling)
            if (!Put(out, scroll.first) || !Put(out, scroll.second)) return false;
        if (!Put(out, std::uint32_t(source.timing.pauses.size()))) return false;
        for (const auto &pause : source.timing.pauses)
            if (!Put(out, pause)) return false;
        if (!Put(out, std::uint32_t(source.cues.size()))) return false;
        for (std::size_t cue = 0; cue < source.cues.size(); ++cue)
            if (!Put(out, source.cues[cue]) || !Put(out, source.audio[cue])) return false;
    }
    return bool(out);
}

bool MarcusSoundTimeline::Read(std::istream &in, const std::vector<MarcusScene> &expected)
{
    std::uint32_t magic = 0, count = 0;
    double frameSeconds;
    if (!Get(in, magic) || magic != timingMagic || !Get(in, frameSeconds) ||
        !std::isfinite(frameSeconds) || frameSeconds <= 0 || !Get(in, count) || count != expected.size()) return false;
    MarcusSoundTimeline result(frameSeconds);
    std::size_t records = 0;
    auto readCount = [&](std::uint32_t &size) {
        if (!Get(in, size) || size > 1000000 - records) return false;
        records += size;
        return true;
    };
    for (const auto &info : expected)
    {
        MarcusScene stored;
        Source source;
        std::uint32_t size = 0;
        if (!Get(in, stored) || stored.chunk != info.chunk || stored.first_frame != info.first_frame ||
            stored.last_frame != info.last_frame || !Get(in, source.timing.transition) ||
            !std::isfinite(source.timing.transition) || source.timing.transition < 0 || !readCount(size)) return false;
        for (std::uint32_t i = 0; i < size; ++i)
        {
            long frame;
            double seconds;
            if (!Get(in, frame) || !Get(in, seconds) || !std::isfinite(seconds) || seconds < 0 ||
                !source.timing.scrolling.emplace(frame, seconds).second) return false;
        }
        if (!readCount(size)) return false;
        source.timing.pauses.resize(size);
        for (auto &pause : source.timing.pauses)
            if (!Get(in, pause) || pause.kind < 0 || pause.kind > 3 || pause.milliseconds < 0) return false;
        if (!readCount(size)) return false;
        source.cues.resize(size);
        source.audio.resize(size);
        for (std::uint32_t i = 0; i < size; ++i)
            if (!Get(in, source.cues[i]) || !Get(in, source.audio[i]) ||
                !std::isfinite(source.audio[i].seconds) || source.audio[i].seconds < 0) return false;
        result.AppendPrepared(info, std::move(source));
    }
    if (in.peek() != std::char_traits<char>::eof()) return false;
    *this = std::move(result);
    return true;
}

MarcusSoundTimeline::Schedule MarcusSoundTimeline::Create(double frameSeconds)
{
    Schedule result(marcus_sound_schedule_create(frameSeconds), marcus_sound_schedule_destroy);
    if (!result) throw std::bad_alloc();
    return result;
}

MarcusSoundTimeline::MarcusSoundTimeline() : MarcusSoundTimeline(1.0 / 6)
{
}

MarcusSoundTimeline::MarcusSoundTimeline(double frameSeconds) : schedule(Create(frameSeconds))
{
}

bool MarcusSoundTimeline::Reframe(const std::vector<MarcusScene> &ranges)
{
    const auto count = std::min(sources.size(), ranges.size());
    for (std::size_t i = 0; i < count; ++i)
        if (ranges[i].chunk != sources[i].info.chunk) return false;
    auto replacement = Create(marcus_sound_schedule_frame_seconds(schedule.get()));
    for (std::size_t i = 0; i < count; ++i)
        ScheduleScene(replacement.get(), ranges[i], sources[i]);
    for (std::size_t i = 0; i < count; ++i)
        sources[i].info = ranges[i];
    sources.resize(count);
    schedule = std::move(replacement);
    return true;
}

void MarcusSoundTimeline::Clear()
{
    marcus_sound_schedule_clear(schedule.get());
    sources.clear();
}

void MarcusSoundTimeline::SetRate(double fps)
{
    marcus_sound_schedule_rate(schedule.get(), fps);
    sources.clear();
}

MarcusSoundScene::MarcusSoundScene(const MarcusSoundTimeline &owner, std::size_t index)
    : start(0), info(owner.sources.at(index).info), owner(owner), index(index)
{
    start = TimeAt(info.first_frame);
}

double MarcusSoundScene::TimeAt(double frame) const
{
    return marcus_sound_schedule_time_at(owner.schedule.get(), index, frame);
}

double MarcusSoundScene::FrameAt(double seconds) const
{
    return marcus_sound_schedule_frame_at(owner.schedule.get(), index, seconds);
}

std::size_t MarcusSoundScene::SoundCount() const
{
    return marcus_sound_schedule_sound_count(owner.schedule.get(), index);
}

MarcusSoundSpan MarcusSoundScene::Sound(std::size_t row) const
{
    const auto span = marcus_sound_schedule_sound(owner.schedule.get(), index, row);
    return {owner.sources[span.scene].cues[span.source_row], span.scene, span.start, span.end};
}

std::vector<MarcusResumeSound> MarcusSoundTimeline::Resume(std::size_t index, long frame) const
{
    std::vector<MarcusScheduledResume> pending(Scene(index).SoundCount());
    std::size_t count;
    if (!marcus_sound_schedule_resume(schedule.get(), index, frame, pending.data(), &count))
        throw std::bad_alloc();
    std::vector<MarcusResumeSound> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto &sound = pending[i];
        auto cue = sources[sound.scene].cues[sound.source_row];
        cue.queue = sound.queue != 0;
        result.push_back({cue, sound.seconds});
    }
    return result;
}

void MarcusSoundTimeline::Append(const MarcusScene &info, const std::vector<MM::PreviewSoundCue> &cues,
                                const std::function<MarcusWaveformInfo(const MM::PreviewSoundCue &)> &read,
                                const MarcusSoundTiming &timing)
{
    Source source;
    source.cues = cues;
    source.timing = timing;
    source.audio.reserve(cues.size());
    for (const auto &cue : cues) source.audio.push_back(read(cue));
    AppendPrepared(info, std::move(source));
}

void MarcusSoundTimeline::AppendPrepared(const MarcusScene &info, Source source)
{
    source.info = info;
    sources.push_back(std::move(source));
    try
    {
        ScheduleScene(schedule.get(), info, sources.back());
    }
    catch (...)
    {
        sources.pop_back();
        throw;
    }
}

void MarcusSoundTimeline::ScheduleScene(MarcusSoundSchedule *schedule, const MarcusScene &info, const Source &source)
{
    std::vector<MarcusSoundEvent> events;
    events.reserve(source.cues.size() + source.timing.scrolling.size() + source.timing.pauses.size());
    for (std::size_t i = 0; i < source.cues.size(); ++i)
    {
        const auto &cue = source.cues[i];
        const auto &audio = source.audio[i];
        MarcusSoundEvent event = {};
        event.frame = cue.frame;
        event.kind = MARCUS_SCHEDULE_CUE;
        event.data.cue = {cue.actor, cue.style, cue.actorSound, cue.loop, cue.queue, cue.motion,
                          audio.kind, audio.seconds, audio.resolved_type, audio.resolved_chunk, audio.resolved_source, i};
        events.push_back(event);
    }
    for (const auto &scroll : source.timing.scrolling)
    {
        MarcusSoundEvent event = {};
        event.frame = scroll.first;
        event.kind = MARCUS_SCHEDULE_SCROLL;
        event.data.scroll = scroll.second;
        events.push_back(event);
    }
    for (const auto &pause : source.timing.pauses)
    {
        MarcusSoundEvent event = {};
        event.frame = pause.frame;
        event.kind = MARCUS_SCHEDULE_PAUSE;
        event.data.pause = {pause.kind, pause.milliseconds};
        events.push_back(event);
    }
    if (!marcus_sound_schedule_append(schedule, info.first_frame, info.last_frame,
                                      source.timing.transition, events.data(), events.size()))
        throw std::bad_alloc();
}
