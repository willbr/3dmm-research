#include "sound_timeline.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <istream>
#include <ostream>
#include <type_traits>

namespace
{
bool SameLane(const MM::PreviewSoundCue &a, const MM::PreviewSoundCue &b)
{
    return a.actorSound == b.actorSound && (!a.actorSound || a.actor == b.actor) && a.style == b.style;
}
bool SameAudio(const MarcusWaveformInfo &a, const MarcusWaveformInfo &b)
{
    return a.resolved_type == b.resolved_type && a.resolved_chunk == b.resolved_chunk &&
           a.resolved_source == b.resolved_source;
}
} // namespace
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
    if (sources.size() != scenes.size() || !Put(out, timingMagic) || !Put(out, frameSeconds) ||
        !Put(out, std::uint32_t(scenes.size()))) return false;
    for (std::size_t index = 0; index < sources.size(); ++index)
    {
        const auto &source = sources[index];
        if (!Put(out, scenes[index].info) || !Put(out, source.timing.transition) ||
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
    MarcusSoundTimeline result;
    std::uint32_t magic = 0, count = 0;
    if (!Get(in, magic) || magic != timingMagic || !Get(in, result.frameSeconds) ||
        !std::isfinite(result.frameSeconds) || result.frameSeconds <= 0 ||
        !Get(in, count) || count != expected.size()) return false;
    std::size_t records = 0;
    auto readCount = [&](std::uint32_t &size) { return Get(in, size) && (records += size) <= 1000000; };
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
        result.AppendPrepared(info, source);
        result.sources.push_back(std::move(source));
    }
    if (in.peek() != std::char_traits<char>::eof()) return false;
    *this = std::move(result);
    return true;
}
bool MarcusSoundTimeline::Reframe(const std::vector<MarcusScene> &ranges)
{
    MarcusSoundTimeline result;
    result.frameSeconds = frameSeconds;
    for (std::size_t i = 0; i < std::min(sources.size(), ranges.size()); ++i)
    {
        if (ranges[i].chunk != scenes[i].info.chunk) return false;
        result.AppendPrepared(ranges[i], sources[i]);
    }
    sources.resize(std::min(sources.size(), ranges.size()));
    result.sources = std::move(sources);
    *this = std::move(result);
    return true;
}
void MarcusSoundTimeline::Clear()
{
    sources.clear();
    scenes.clear();
    active.clear();
    time = 0;
    transition = 0;
}
void MarcusSoundTimeline::SetRate(double fps)
{
    Clear();
    // Uncapped throughput depends on the host. Keep a nominal six-FPS preview.
    frameSeconds = fps > 0 ? 1.0 / fps : 1.0 / 6;
}
double MarcusSoundScene::TimeAt(double frame) const
{
    double result = start + (frame - info.first_frame) * frameSeconds;
    for (const auto &hold : holds)
    {
        if (frame <= hold.first)
            break;
        result += std::min(1.0, frame - hold.first) * hold.second;
    }
    return result;
}
double MarcusSoundScene::FrameAt(double seconds) const
{
    double frame = info.first_frame, at = start;
    for (const auto &hold : holds)
    {
        const double entry = at + (hold.first - frame) * frameSeconds;
        if (seconds < entry)
            return frame + (seconds - at) / frameSeconds;
        const double duration = frameSeconds + hold.second;
        if (seconds < entry + duration)
            return hold.first + (seconds - entry) / duration;
        frame = double(hold.first) + 1;
        at = entry + duration;
    }
    return frame + (seconds - at) / frameSeconds;
}
std::vector<MarcusResumeSound> MarcusSoundTimeline::Resume(std::size_t index, long frame) const
{
    const auto &scene = scenes.at(index);
    const double at = scene.TimeAt(frame);
    std::vector<std::shared_ptr<MarcusSoundSpan>> pending;
    for (const auto &span : scene.sounds)
        if (!span->cue.motion && span->end > at && std::isfinite(span->start) &&
            (span->scene < static_cast<int>(index) || span->cue.frame < frame))
            pending.push_back(span);
    std::stable_sort(pending.begin(), pending.end(), [](const auto &a, const auto &b) {
        return a->start < b->start;
    });
    std::vector<MarcusResumeSound> result;
    for (const auto &span : pending)
    {
        auto cue = span->cue;
        cue.queue = std::any_of(result.begin(), result.end(), [&](const auto &sound) {
            return SameLane(sound.cue, cue);
        });
        result.push_back({cue, std::max(0.0, at - span->start)});
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
    sources.reserve(sources.size() + 1);
    AppendPrepared(info, source);
    sources.push_back(std::move(source));
}
void MarcusSoundTimeline::AppendPrepared(const MarcusScene &info, const Source &source)
{
    const auto &cues = source.cues;
    const auto &timing = source.timing;
    MarcusSoundScene scene;
    scene.info = info;
    scene.frameSeconds = frameSeconds;
    for (const auto &sound : active)
        if (sound->cue.loop && sound->audio.kind != MARCUS_WAVEFORM_MIDI)
            sound->end = std::min(sound->end, time);
    active.erase(std::remove_if(active.begin(), active.end(), [&](const auto &sound) {
        return sound->end <= time;
    }), active.end());
    // Removing a sampled loop can unblock the sounds queued behind it.
    for (std::size_t index = 0; index < active.size(); ++index)
        if (!std::isfinite(active[index]->start))
        {
            auto &sound = active[index];
            sound->start = time;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (SameLane(active[prior]->cue, sound->cue))
                    sound->start = std::max(sound->start, active[prior]->end);
            sound->end = sound->cue.loop ? std::numeric_limits<double>::infinity()
                                        : sound->start + sound->audio.seconds;
        }
    time += transition;
    scene.start = time;
    active.erase(std::remove_if(active.begin(), active.end(), [&](const auto &sound) {
        return sound->end <= time;
    }), active.end());
    scene.sounds = active;
    std::map<long, std::vector<std::size_t>> frames;
    std::map<long, MM::PreviewPause> pauses;
    for (std::size_t index = 0; index < cues.size(); ++index)
        if (cues[index].frame <= info.last_frame)
            frames[std::max<long>(info.first_frame, cues[index].frame)].push_back(index);
    for (const auto &scroll : timing.scrolling)
        if (scroll.first >= info.first_frame && scroll.first <= info.last_frame)
            frames[scroll.first];
    for (const auto &pause : timing.pauses)
        if (pause.frame >= info.first_frame && pause.frame <= info.last_frame)
        {
            frames[pause.frame];
            pauses[pause.frame] = pause;
        }
    for (const auto &frame : frames)
    {
        const double at = scene.TimeAt(frame.first);
        active.erase(std::remove_if(active.begin(), active.end(), [&](const auto &sound) { return sound->end <= at; }),
                     active.end());
        for (const auto index : frame.second)
        {
            const auto &cue = cues[index];
            auto span = std::make_shared<MarcusSoundSpan>();
            span->cue = cue;
            span->audio = source.audio[index];
            span->scene = static_cast<int>(scenes.size());
            span->start = at;
            const bool playable =
                span->audio.kind == MARCUS_WAVEFORM_SAMPLED || span->audio.kind == MARCUS_WAVEFORM_MIDI;
            const bool duplicateMusic = span->audio.kind == MARCUS_WAVEFORM_MIDI &&
                                        std::any_of(active.begin(), active.end(), [&](const auto &sound) {
                                            return SameLane(sound->cue, cue) &&
                                                   sound->audio.kind == MARCUS_WAVEFORM_MIDI &&
                                                   SameAudio(sound->audio, span->audio);
                                        });
            if (duplicateMusic)
            {
                // Retain the authored marker while the original music voice plays.
                span->end = at;
                scene.sounds.push_back(span);
                continue;
            }
            if (!cue.queue || !playable || span->audio.seconds <= 0)
            {
                for (const auto &sound : active)
                    if (SameLane(sound->cue, cue))
                        sound->end = std::min(sound->end, at);
                active.erase(std::remove_if(active.begin(), active.end(),
                                            [&](const auto &sound) { return SameLane(sound->cue, cue); }),
                             active.end());
            }
            else
                for (const auto &sound : active)
                    if (SameLane(sound->cue, cue))
                        span->start = std::max(span->start, sound->end);
            span->end = !playable                             ? span->start
                        : cue.loop && span->audio.seconds > 0 ? std::numeric_limits<double>::infinity()
                                                              : span->start + span->audio.seconds;
            scene.sounds.push_back(span);
            if (playable && span->audio.seconds > 0)
                active.push_back(span);
        }
        const auto scroll = timing.scrolling.find(frame.first);
        double hold = scroll == timing.scrolling.end() ? 0 : scroll->second;
        const auto pause = pauses.find(frame.first);
        if (pause != pauses.end())
        {
            if (pause->second.kind == 3)
                hold += pause->second.milliseconds / 1000.0;
            else if (pause->second.kind == 2)
                for (const auto &sound : active)
                    if (!sound->cue.loop && std::isfinite(sound->end))
                        hold = std::max(hold, sound->end - at);
            // Click waits have no authored duration; assume immediate acknowledgement.
        }
        if (hold > 0)
            scene.holds[frame.first] = hold;
    }
    const double finish = scene.TimeAt(double(info.last_frame) + 1);
    // Include tails that intersect this scene, retaining authored markers even
    // when another event in the same frame replaces them immediately.
    scene.sounds.erase(std::remove_if(scene.sounds.begin(), scene.sounds.end(), [&](const auto &sound) {
        return sound->scene != static_cast<int>(scenes.size()) &&
               (sound->end <= time || sound->start >= finish);
    }), scene.sounds.end());
    scenes.push_back(std::move(scene));
    time = finish;
    transition = timing.transition;
}
