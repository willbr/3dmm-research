#ifndef MM_PREVIEW_CUE_VIEW_H
#define MM_PREVIEW_CUE_VIEW_H
#include "mm/preview_metadata.h"
namespace MM
{
namespace Internal
{
inline PreviewSoundCue PreviewCue(const MMPreviewCue &c)
{
    PreviewSoundCue out;
    out.frame = c.frame;
    out.source = c.source;
    out.actor = c.actor;
    out.style = c.style;
    out.volume = c.volume;
    out.sound = {c.type, c.number};
    out.scene = {c.scene_type, c.scene_number};
    out.child = c.child;
    out.actorSound = c.actor_sound != 0;
    out.loop = c.loop != 0;
    out.queue = c.queue != 0;
    out.silent = c.silent != 0;
    out.motion = c.motion != 0;
    out.motionCel = c.motion_cel;
    return out;
}
inline MMPreviewCue PreviewCueCore(const PreviewSoundCue &c)
{
    MMPreviewCue out = {};
    out.frame = c.frame;
    out.source = c.source;
    out.actor = c.actor;
    out.style = c.style;
    out.volume = c.volume;
    out.type = c.sound.type;
    out.number = c.sound.number;
    out.scene_type = c.scene.type;
    out.scene_number = c.scene.number;
    out.child = c.child;
    out.actor_sound = c.actorSound;
    out.loop = c.loop;
    out.queue = c.queue;
    out.silent = c.silent;
    out.motion = c.motion;
    out.motion_cel = c.motionCel;
    return out;
}
inline void OrderPreviewCues(std::vector<PreviewSoundCue> &cues, Id scene)
{
    MMPreviewCueOrder order = {};
    order.context = &cues;
    order.count = static_cast<MMU32>(cues.size());
    order.get = [](void *p, MMU32 i, MMPreviewCue *out) {
        *out = PreviewCueCore((*static_cast<std::vector<PreviewSoundCue> *>(p))[i]);
    };
    order.put = [](void *p, MMU32 i, const MMPreviewCue *value) {
        (*static_cast<std::vector<PreviewSoundCue> *>(p))[i] = PreviewCue(*value);
    };
    mm_preview_cues_sort(scene.type, scene.number, &order);
}
} // namespace Internal
} // namespace MM
#endif
