#include "sound_timeline.h"
#include <algorithm>
#include <exception>
#include <limits>
#include <istream>
#include <new>
#include <ostream>
#include <stdexcept>

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
static_assert(int(MARCUS_WAVEFORM_SAMPLED) == MM_SOUND_CLIP_SAMPLED &&
              int(MARCUS_WAVEFORM_MIDI) == MM_SOUND_CLIP_MIDI, "schedule audio kinds");

MM::PreviewSoundCue PreviewCue(const MMMovieSoundSchedule *schedule, std::size_t scene, std::size_t row)
{
    MMMovieTimingScene source;
    mm_movie_sound_schedule_source(schedule, scene, &source);
    return MarcusPreviewCue(source.cues[row]);
}
} // namespace

MM::PreviewSoundCue MarcusPreviewCue(const MMMovieTimingCue &cue)
{
    return {cue.frame, cue.source, cue.actor, cue.style, cue.volume,
            {cue.sound_type, cue.sound_number}, {cue.scene_type, cue.scene_number}, cue.child,
            bool(cue.flags & MM_TIMING_ACTOR_SOUND), bool(cue.flags & MM_TIMING_LOOP),
            bool(cue.flags & MM_TIMING_QUEUE), bool(cue.flags & MM_TIMING_SILENT),
            bool(cue.flags & MM_TIMING_MOTION), cue.motion_cel};
}

bool MarcusSoundTimeline::Write(std::ostream &out) const
{
    struct Context
    {
        std::ostream &output;
        const MMMovieSoundSchedule *schedule;
        std::exception_ptr failure;
    } context{out, schedule.get(), {}};
    const MMMovieTimingWriter writer{
        &context,
        [](void *p, const void *bytes, std::size_t size) -> int {
            auto &ctx = *static_cast<Context *>(p);
            try { return bool(ctx.output.write(static_cast<const char *>(bytes), size)); }
            catch (...) { ctx.failure = std::current_exception(); return false; }
        },
        [](void *p, std::size_t index, MMMovieTimingScene *scene) -> int {
            mm_movie_sound_schedule_source(static_cast<Context *>(p)->schedule, index, scene);
            return true;
        }};
    const int written = mm_movie_timing_snapshot_write(&writer, Size(),
                                                       mm_movie_sound_schedule_frame_seconds(schedule.get()));
    if (context.failure) std::rethrow_exception(context.failure);
    return written != 0;
}

bool MarcusSoundTimeline::Read(std::istream &in, const std::vector<MarcusScene> &expected)
{
    struct Context
    {
        std::istream &input;
        const std::vector<MarcusScene> &expected;
        Schedule schedule;
        std::exception_ptr failure;
    } context{in, expected, Schedule(nullptr, mm_movie_sound_schedule_destroy), {}};
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
            return mm_movie_sound_schedule_append(static_cast<Context *>(p)->schedule.get(), scene);
        },
        [](void *p, double seconds) -> int {
            auto &ctx = *static_cast<Context *>(p);
            ctx.schedule.reset(mm_movie_sound_schedule_create(seconds));
            return bool(ctx.schedule);
        }};
    double frameSeconds;
    const int read = mm_movie_timing_snapshot_read(&reader, expected.size(), &frameSeconds);
    if (context.failure) std::rethrow_exception(context.failure);
    if (!read) return false;
    auto metadata = expected;
    scenes = std::move(metadata);
    schedule = std::move(context.schedule);
    return true;
}

MarcusSoundTimeline::Schedule MarcusSoundTimeline::Create(double frameSeconds)
{
    Schedule result(mm_movie_sound_schedule_create(frameSeconds), mm_movie_sound_schedule_destroy);
    if (!result) throw std::bad_alloc();
    return result;
}

MarcusSoundTimeline::MarcusSoundTimeline() : schedule(Create(1.0 / 6))
{
}

MarcusSoundTimeline::MarcusSoundTimeline(Schedule data, std::vector<MarcusScene> metadata)
    : schedule(std::move(data)), scenes(std::move(metadata))
{
}

bool MarcusSoundTimeline::Reframe(const MarcusScene *ranges, std::size_t available)
{
    auto replacement = Reframed(ranges, available);
    if (!replacement) return false;
    *this = std::move(*replacement);
    return true;
}

std::optional<MarcusSoundTimeline> MarcusSoundTimeline::Reframed(const MarcusScene *ranges, std::size_t available) const
{
    const auto count = std::min(Size(), available);
    std::vector<MMMovieTimingRange> geometry(count);
    for (std::size_t i = 0; i < count; ++i)
        if (!TimingRange(ranges[i], geometry[i])) return std::nullopt;
    MMMovieSoundSchedule *result = nullptr;
    const int status = mm_movie_sound_schedule_reframe(schedule.get(), geometry.data(), geometry.size(), &result);
    if (status == MM_SOUND_REFRAME_MISMATCH) return std::nullopt;
    if (status == MM_SOUND_REFRAME_MEMORY) throw std::bad_alloc();
    Schedule replacement(result, mm_movie_sound_schedule_destroy);
    std::vector<MarcusScene> metadata;
    if (count) metadata.assign(ranges, ranges + count);
    return MarcusSoundTimeline(std::move(replacement), std::move(metadata));
}

void MarcusSoundTimeline::Clear()
{
    mm_movie_sound_schedule_clear(schedule.get());
    scenes.clear();
}

void MarcusSoundTimeline::SetRate(double fps)
{
    mm_movie_sound_schedule_rate(schedule.get(), fps);
    scenes.clear();
}

MarcusSoundScene::MarcusSoundScene(const MarcusSoundTimeline &owner, std::size_t index)
    : start(0), info(owner.scenes.at(index)), owner(owner), index(index)
{
    start = TimeAt(info.first_frame);
}

double MarcusSoundScene::TimeAt(double frame) const
{
    return mm_movie_sound_schedule_time_at(owner.schedule.get(), index, frame);
}

double MarcusSoundScene::FrameAt(double seconds) const
{
    return mm_movie_sound_schedule_frame_at(owner.schedule.get(), index, seconds);
}

std::size_t MarcusSoundScene::SoundCount() const
{
    return mm_movie_sound_schedule_sound_count(owner.schedule.get(), index);
}

MarcusSoundSpan MarcusSoundScene::Sound(std::size_t row) const
{
    const auto span = mm_movie_sound_schedule_sound(owner.schedule.get(), index, row);
    return {PreviewCue(owner.schedule.get(), span.scene, span.source_row), span.scene, span.start, span.end};
}

int MarcusSoundScene::BuildSheet(MarcusSoundSheet &sheet, const MarcusSoundSheetNames &names) const
{
    return marcus_sound_sheet_build(&sheet, owner.schedule.get(), index, &names);
}

void MarcusSoundScene::ChapterTime(MarcusSceneTime &time) const
{
    mm_movie_sound_schedule_scene_time(owner.schedule.get(), index, &time.start_seconds, &time.end_seconds);
}

void MarcusSoundScene::MovieTime(MMI32 frame, double elapsed, bool finished, MarcusMovieTime &time) const
{
    mm_movie_sound_schedule_movie_time(owner.schedule.get(), index, frame, elapsed, finished,
                                       &time.duration_seconds, &time.position_seconds);
}

bool MarcusSoundTimeline::Locate(double seconds, MMMovieSoundPosition &position) const
{
    return mm_movie_sound_schedule_locate(schedule.get(), seconds, &position) != 0;
}

std::vector<MarcusResumeSound> MarcusSoundTimeline::Resume(std::size_t index, long frame) const
{
    std::vector<MMScheduledResume> pending(Scene(index).SoundCount());
    std::size_t count;
    if (!mm_movie_sound_schedule_resume(schedule.get(), index, static_cast<double>(frame), pending.data(), &count))
        throw std::bad_alloc();
    std::vector<MarcusResumeSound> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto &sound = pending[i];
        auto cue = PreviewCue(schedule.get(), sound.scene, sound.source_row);
        cue.queue = sound.queue != 0;
        result.push_back({cue, sound.seconds});
    }
    return result;
}

void MarcusSoundTimeline::Append(const MarcusScene &info, const std::vector<MM::PreviewSoundCue> &cues,
                                const std::function<MarcusWaveformInfo(const MM::PreviewSoundCue &)> &read,
                                const MMMovieTimingHolds &timing)
{
    MMMovieTimingRange range;
    if (!TimingRange(info, range))
        throw std::out_of_range("Movie sound scene is outside the native range.");
    struct Context
    {
        const std::vector<MM::PreviewSoundCue> &cues;
        const std::function<MarcusWaveformInfo(const MM::PreviewSoundCue &)> &read;
        std::exception_ptr failure;
    } context{cues, read, {}};
    scenes.push_back(info);
    const int status = mm_movie_sound_schedule_append_cues(schedule.get(), &range, &timing, cues.size(), &context,
        [](void *p, std::size_t index, MMMovieTimingCue *out) -> int {
            auto &h = *static_cast<Context *>(p);
            try { return TimingCue(h.cues[index], h.read(h.cues[index]), *out); }
            catch (...) { h.failure = std::current_exception(); return 0; }
        });
    if (status != MM_SOUND_COLLECT_OK)
    {
        scenes.pop_back();
        if (context.failure) std::rethrow_exception(context.failure);
        if (status == MM_SOUND_COLLECT_SOURCE)
            throw std::out_of_range("Movie sound cue is outside the native range.");
        throw std::bad_alloc();
    }
}
