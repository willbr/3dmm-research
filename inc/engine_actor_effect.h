#ifndef MM_ENGINE_ACTOR_EFFECT_HOST_H
#define MM_ENGINE_ACTOR_EFFECT_HOST_H

#include "soc_core.h"
#include "mm/actor_effect.h"
#include "mm/actor_sound.h"
#include "mm/actor_events.h"

struct EngineActorEffectHost
{
    PTemplate tmpl;
    PBody body;
    MMActorSounds *sounds;
    void *owner;
    ScopedBodyCostume saved;

    static EngineActorEffectHost &From(void *p) { return *static_cast<EngineActorEffectHost *>(p); }

    MMActorEffectServices Services()
    {
        MMActorEffectServices s = {};
        s.context = this;
        s.write_event = [](void *events, MMI32 index, const MMActorEvent *event) {
            mm_actor_events_set_fixed(static_cast<MMActorEventTable *>(events), index, event);
        };
        s.select_cel = [](void *p, MMI32 action, MMI32 cel) -> int {
            auto &h = From(p);
            return mm_brender_template_set_cel(h.tmpl, h.body, action, cel, nullptr);
        };
        s.cel_count = [](void *p, MMI32 action, MMI32 *out) -> int {
            return mm_brender_template_cel_count(From(p).tmpl, action, out);
        };
        s.action_flags = [](void *p, MMI32 action, MMU32 *out) -> int {
            return mm_brender_template_action_flags(From(p).tmpl, action, out);
        };
        s.save_costume = [](void *p) -> int { auto &h = From(p); return h.saved.FGet(h.body); };
        s.default_costume = [](void *p) -> int { auto &h = From(p); return mm_brender_template_default_costume(h.tmpl, h.body); };
        s.restore_costume = [](void *p) { auto &h = From(p); h.saved.Set(h.body); };
        s.fetch_custom = [](void *p, MMI32 costume) -> void * { return mm_brender_template_costume(From(p).tmpl, costume); };
        s.apply_custom = [](void *p, void *resource) {
            mm_body_set_custom(From(p).body, resource, &mm_kauai_body_renderer()->edit.runtime);
        };
        s.apply_material = [](void *p, MMI32 set, void *resource) {
            mm_body_set_generic(From(p).body, set, resource, &mm_kauai_body_renderer()->edit.runtime);
        };
        s.release_resource = [](void *, void *resource, int) {
            mm_material_owner_release(static_cast<MMMaterialOwner *>(resource));
        };
        s.clear_sounds = [](void *p) { mm_actor_sounds_clear(From(p).sounds); };
        return s;
    }
};

#endif
