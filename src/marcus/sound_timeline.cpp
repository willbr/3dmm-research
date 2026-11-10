#include "sound_timeline.h"
#include <algorithm>
#include <cmath>
#include <limits>

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
}
void MarcusSoundTimeline::Clear()
{
    scenes.clear();
    active.clear();
    time = 0;
}
void MarcusSoundTimeline::Append(const MarcusScene &info, const std::vector<ThreeMM::PreviewSoundCue> &cues,
                                const std::function<MarcusWaveformInfo(const ThreeMM::PreviewSoundCue &)> &read)
{
    MarcusSoundScene scene;
    scene.start = time;
    scene.info = info;
    const double finish = time + (double(info.last_frame) - info.first_frame + 1) / 6;
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
    scene.sounds = active;
    for (const auto &cue : cues)
    {
        if (cue.frame > info.last_frame)
            continue;
        const double at = time + std::max(0.0, (double(cue.frame) - info.first_frame) / 6);
        active.erase(std::remove_if(active.begin(), active.end(), [&](const auto &sound) {
            return sound->end <= at;
        }), active.end());
        auto span = std::make_shared<MarcusSoundSpan>();
        span->cue = cue;
        span->audio = read(cue);
        span->scene = static_cast<int>(scenes.size());
        span->start = at;
        const bool playable = span->audio.kind == MARCUS_WAVEFORM_SAMPLED ||
                              span->audio.kind == MARCUS_WAVEFORM_MIDI;
        const bool duplicateMusic = span->audio.kind == MARCUS_WAVEFORM_MIDI &&
            std::any_of(active.begin(), active.end(), [&](const auto &sound) {
                return SameLane(sound->cue, cue) && sound->audio.kind == MARCUS_WAVEFORM_MIDI &&
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
            active.erase(std::remove_if(active.begin(), active.end(), [&](const auto &sound) {
                return SameLane(sound->cue, cue);
            }), active.end());
        }
        else
            for (const auto &sound : active)
                if (SameLane(sound->cue, cue))
                    span->start = std::max(span->start, sound->end);
        span->end = !playable ? span->start
                    : cue.loop && span->audio.seconds > 0 ? std::numeric_limits<double>::infinity()
                    : span->start + span->audio.seconds;
        scene.sounds.push_back(span);
        if (playable && span->audio.seconds > 0)
            active.push_back(span);
    }
    // Include tails that intersect this scene, retaining authored markers even
    // when another event in the same frame replaces them immediately.
    scene.sounds.erase(std::remove_if(scene.sounds.begin(), scene.sounds.end(), [&](const auto &sound) {
        return sound->scene != static_cast<int>(scenes.size()) &&
               (sound->end <= time || sound->start >= finish);
    }), scene.sounds.end());
    scenes.push_back(std::move(scene));
    time = finish;
}
