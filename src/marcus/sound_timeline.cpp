#include "sound_timeline.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <istream>
#include <ostream>
#include <type_traits>

namespace
{
bool SameLane(const ThreeMM::PreviewSoundCue &a, const ThreeMM::PreviewSoundCue &b)
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
constexpr std::uint32_t timingMagic = 0x31544d4d; // MMT1, matching-build ephemeral IPC.
} // namespace
bool MarcusSoundTimeline::Write(std::ostream &out) const
{
    if (!Put(out, timingMagic) || !Put(out, std::uint32_t(scenes.size())))
        return false;
    for (const auto &scene : scenes)
    {
        if (!Put(out, scene.info) || !Put(out, scene.start) || !Put(out, std::uint32_t(scene.holds.size())))
            return false;
        for (const auto &hold : scene.holds)
            if (!Put(out, hold.first) || !Put(out, hold.second))
                return false;
        if (!Put(out, std::uint32_t(scene.sounds.size())))
            return false;
        for (const auto &span : scene.sounds)
            if (!Put(out, *span))
                return false;
    }
    return bool(out);
}
bool MarcusSoundTimeline::Read(std::istream &in, const std::vector<MarcusScene> &expected)
{
    MarcusSoundTimeline result;
    std::uint32_t magic = 0, count = 0;
    if (!Get(in, magic) || magic != timingMagic || !Get(in, count) || count != expected.size())
        return false;
    std::size_t records = 0;
    for (const auto &info : expected)
    {
        MarcusSoundScene scene;
        std::uint32_t holds = 0, sounds = 0;
        if (!Get(in, scene.info) || scene.info.chunk != info.chunk || scene.info.first_frame != info.first_frame ||
            scene.info.last_frame != info.last_frame || !Get(in, scene.start) || !std::isfinite(scene.start) ||
            scene.start < 0 || !Get(in, holds) || (records += holds) > 1000000)
            return false;
        scene.info = info;
        for (std::uint32_t i = 0; i < holds; ++i)
        {
            long frame;
            double seconds;
            if (!Get(in, frame) || !Get(in, seconds) || frame < info.first_frame || frame > info.last_frame ||
                !std::isfinite(seconds) || seconds < 0 || !scene.holds.emplace(frame, seconds).second)
                return false;
        }
        if (!Get(in, sounds) || (records += sounds) > 1000000)
            return false;
        for (std::uint32_t i = 0; i < sounds; ++i)
        {
            auto span = std::make_shared<MarcusSoundSpan>();
            if (!Get(in, *span) || span->scene < 0 || static_cast<std::size_t>(span->scene) >= expected.size() ||
                std::isnan(span->start) || std::isnan(span->end) || !std::isfinite(span->audio.seconds) ||
                span->audio.seconds < 0)
                return false;
            scene.sounds.push_back(std::move(span));
        }
        result.scenes.push_back(std::move(scene));
    }
    if (in.peek() != std::char_traits<char>::eof())
        return false;
    *this = std::move(result);
    return true;
}
void MarcusSoundTimeline::Clear()
{
    scenes.clear();
    active.clear();
    time = 0;
    transition = 0;
}
double MarcusSoundScene::TimeAt(double frame) const
{
    double result = start + (frame - info.first_frame) / 6;
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
        const double entry = at + (hold.first - frame) / 6;
        if (seconds < entry)
            return frame + (seconds - at) * 6;
        const double duration = 1.0 / 6 + hold.second;
        if (seconds < entry + duration)
            return hold.first + (seconds - entry) / duration;
        frame = double(hold.first) + 1;
        at = entry + duration;
    }
    return frame + (seconds - at) * 6;
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
void MarcusSoundTimeline::Append(const MarcusScene &info, const std::vector<ThreeMM::PreviewSoundCue> &cues,
                                const std::function<MarcusWaveformInfo(const ThreeMM::PreviewSoundCue &)> &read,
                                const MarcusSoundTiming &timing)
{
    MarcusSoundScene scene;
    scene.info = info;
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
    std::map<long, std::vector<ThreeMM::PreviewSoundCue>> frames;
    std::map<long, ThreeMM::PreviewPause> pauses;
    for (const auto &cue : cues)
        if (cue.frame <= info.last_frame)
            frames[std::max<long>(info.first_frame, cue.frame)].push_back(cue);
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
        for (const auto &cue : frame.second)
        {
            auto span = std::make_shared<MarcusSoundSpan>();
            span->cue = cue;
            span->audio = read(cue);
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
