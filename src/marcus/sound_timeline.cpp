#include "sound_timeline.h"
#include <algorithm>
#include "mm/movie_timing_snapshot.h"
#include <exception>
#include <limits>
#include <istream>
#include <new>
#include <ostream>

namespace
{
bool FitsFrame(long value)
{
    return value >= (std::numeric_limits<MMI32>::min)() && value <= (std::numeric_limits<MMI32>::max)();
}
bool TimingRange(const MarcusScene &scene, MMMovieTimingRange &range)
{
    if (scene.chunk > 0xffffffffUL || !FitsFrame(scene.first_frame) || !FitsFrame(scene.last_frame))
        return false;
    range = {MMU32(scene.chunk), MMI32(scene.first_frame), MMI32(scene.last_frame)};
    return true;
}
bool TimingCue(const MM::PreviewSoundCue &cue, const MarcusWaveformInfo &audio, MMMovieTimingCue &out)
{
    if (audio.resolved_type > 0xffffffffUL || audio.resolved_chunk > 0xffffffffUL ||
        !FitsFrame(audio.resolved_source))
        return false;
    const MMU32 flags = (cue.actorSound ? MM_TIMING_ACTOR_SOUND : 0) | (cue.loop ? MM_TIMING_LOOP : 0) |
                        (cue.queue ? MM_TIMING_QUEUE : 0) | (cue.silent ? MM_TIMING_SILENT : 0) |
                        (cue.motion ? MM_TIMING_MOTION : 0);
    out = {cue.frame, cue.source, cue.actor, cue.style, cue.volume, cue.sound.type, cue.sound.number,
           cue.scene.type, cue.scene.number, cue.child, flags, cue.motionCel, audio.kind,
           MMU32(audio.resolved_type), MMU32(audio.resolved_chunk), MMI32(audio.resolved_source), audio.seconds};
    return true;
}
} // namespace

bool MarcusSoundTimeline::Write(std::ostream &out) const
{
    struct Context
    {
        std::ostream &output;
        const std::vector<Source> &sources;
        std::vector<MMMovieTimingScroll> scrolls;
        std::vector<MMMovieTimingPause> pauses;
        std::vector<MMMovieTimingCue> cues;
        std::exception_ptr failure;
    } context{out, sources, {}, {}, {}, {}};
    const MMMovieTimingWriter writer{
        &context,
        [](void *p, const void *bytes, std::size_t size) -> int {
            auto &ctx = *static_cast<Context *>(p);
            try { return bool(ctx.output.write(static_cast<const char *>(bytes), size)); }
            catch (...) { ctx.failure = std::current_exception(); return false; }
        },
        [](void *p, std::size_t index, MMMovieTimingScene *scene) -> int {
            auto &ctx = *static_cast<Context *>(p);
            const auto &source = ctx.sources[index];
            try
            {
                if (!TimingRange(source.info, scene->range)) return false;
                ctx.scrolls.clear();
                ctx.scrolls.reserve(source.timing.scrolling.size());
                for (const auto &scroll : source.timing.scrolling)
                {
                    if (!FitsFrame(scroll.first)) return false;
                    ctx.scrolls.push_back({MMI32(scroll.first), scroll.second});
                }
                ctx.pauses.clear();
                ctx.pauses.reserve(source.timing.pauses.size());
                for (const auto &pause : source.timing.pauses)
                    ctx.pauses.push_back({pause.frame, pause.kind, pause.milliseconds});
                ctx.cues.resize(source.cues.size());
                for (std::size_t i = 0; i < ctx.cues.size(); ++i)
                    if (!TimingCue(source.cues[i], source.audio[i], ctx.cues[i])) return false;
                scene->transition = source.timing.transition;
                scene->scroll_count = ctx.scrolls.size();
                scene->pause_count = ctx.pauses.size();
                scene->cue_count = ctx.cues.size();
                scene->scrolls = ctx.scrolls.data();
                scene->pauses = ctx.pauses.data();
                scene->cues = ctx.cues.data();
                return true;
            }
            catch (...) { ctx.failure = std::current_exception(); return false; }
        }};
    const int written = mm_movie_timing_snapshot_write(&writer, sources.size(),
                                                       marcus_sound_schedule_frame_seconds(schedule.get()));
    if (context.failure) std::rethrow_exception(context.failure);
    return written != 0;
}

bool MarcusSoundTimeline::Read(std::istream &in, const std::vector<MarcusScene> &expected)
{
    struct Context
    {
        std::istream &input;
        const std::vector<MarcusScene> &expected;
        std::vector<Source> sources;
        std::exception_ptr failure;
    } context{in, expected, {}, {}};
    const MMMovieTimingReader reader{
        &context,
        [](void *p, void *bytes, std::size_t size) -> int {
            auto &ctx = *static_cast<Context *>(p);
            try { return bool(ctx.input.read(static_cast<char *>(bytes), size)); }
            catch (...) { ctx.failure = std::current_exception(); return false; }
        },
        [](void *p) -> int {
            auto &ctx = *static_cast<Context *>(p);
            try { return ctx.input.peek() == std::char_traits<char>::eof() && !ctx.input.bad(); }
            catch (...) { ctx.failure = std::current_exception(); return false; }
        },
        [](void *p, std::size_t index, MMMovieTimingRange *range) -> int {
            return TimingRange(static_cast<Context *>(p)->expected[index], *range);
        },
        [](void *p, std::size_t, const MMMovieTimingScene *scene) -> int {
            auto &ctx = *static_cast<Context *>(p);
            try
            {
                Source source;
                source.timing.transition = scene->transition;
                for (std::size_t i = 0; i < scene->scroll_count; ++i)
                    source.timing.scrolling.emplace_hint(source.timing.scrolling.end(),
                        scene->scrolls[i].frame, scene->scrolls[i].seconds);
                source.timing.pauses.reserve(scene->pause_count);
                for (std::size_t i = 0; i < scene->pause_count; ++i)
                {
                    const auto &pause = scene->pauses[i];
                    source.timing.pauses.push_back({pause.frame, pause.kind, pause.milliseconds});
                }
                source.cues.reserve(scene->cue_count);
                source.audio.resize(scene->cue_count);
                for (std::size_t i = 0; i < scene->cue_count; ++i)
                {
                    const auto &cue = scene->cues[i];
                    source.cues.push_back({cue.frame, cue.source, cue.actor, cue.style, cue.volume,
                        {cue.sound_type, cue.sound_number}, {cue.scene_type, cue.scene_number}, cue.child,
                        bool(cue.flags & MM_TIMING_ACTOR_SOUND), bool(cue.flags & MM_TIMING_LOOP),
                        bool(cue.flags & MM_TIMING_QUEUE), bool(cue.flags & MM_TIMING_SILENT),
                        bool(cue.flags & MM_TIMING_MOTION), cue.motion_cel});
                    auto &audio = source.audio[i];
                    audio.seconds = cue.seconds;
                    audio.kind = cue.audio_kind;
                    audio.resolved_type = cue.resolved_type;
                    audio.resolved_chunk = cue.resolved_chunk;
                    audio.resolved_source = cue.resolved_source;
                }
                ctx.sources.push_back(std::move(source));
                return true;
            }
            catch (...) { ctx.failure = std::current_exception(); return false; }
        }};
    double frameSeconds;
    const int read = mm_movie_timing_snapshot_read(&reader, expected.size(), &frameSeconds);
    if (context.failure) std::rethrow_exception(context.failure);
    if (!read) return false;
    MarcusSoundTimeline result(frameSeconds);
    result.sources.reserve(context.sources.size());
    for (std::size_t i = 0; i < context.sources.size(); ++i)
        result.AppendPrepared(expected[i], std::move(context.sources[i]));
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

std::optional<MarcusSoundTimeline> MarcusSoundTimeline::Reframed(const std::vector<MarcusScene> &ranges) const
{
    const auto count = std::min(sources.size(), ranges.size());
    for (std::size_t i = 0; i < count; ++i)
        if (ranges[i].chunk != sources[i].info.chunk) return std::nullopt;
    MarcusSoundTimeline result(marcus_sound_schedule_frame_seconds(schedule.get()));
    result.sources.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        result.AppendPrepared(ranges[i], sources[i]);
    return result;
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
