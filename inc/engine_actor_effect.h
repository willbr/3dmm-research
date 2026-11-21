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
            return EngineTemplate::FSetActnCel(h.tmpl, h.body, action, cel);
        };
        s.cel_count = [](void *p, MMI32 action, MMI32 *out) -> int {
            LegacyLong count = *out;
            const auto ok = EngineTemplate::FGetCcelActn(From(p).tmpl, action, &count);
            *out = count;
            return ok;
        };
        s.action_flags = [](void *p, MMI32 action, MMU32 *out) -> int {
            LegacyUlong flags = *out;
            const auto ok = EngineTemplate::FGetGrfactn(From(p).tmpl, action, &flags);
            *out = flags;
            return ok;
        };
        s.save_costume = [](void *p) -> int { auto &h = From(p); return h.saved.FGet(h.body); };
        s.default_costume = [](void *p) -> int { auto &h = From(p); return EngineTemplate::FSetDefaultCost(h.tmpl, h.body); };
        s.restore_costume = [](void *p) { auto &h = From(p); h.saved.Set(h.body); };
        s.fetch_custom = [](void *p, MMI32 costume) -> void * { return EngineTemplate::PcmtlFetch(From(p).tmpl, costume); };
        s.apply_custom = [](void *p, void *resource) {
            EngineBody::SetPartSetCmtl(From(p).body, static_cast<PCustomMaterial_CMTL>(resource));
        };
        s.apply_material = [](void *p, MMI32 set, void *resource) {
            EngineBody::SetPartSetMtrl(From(p).body, set, static_cast<PMaterial_MTRL>(resource));
        };
        s.release_resource = [](void *, void *resource, int) {
            mm_material_owner_release(static_cast<MMMaterialOwner *>(resource));
        };
        s.clear_sounds = [](void *p) { mm_actor_sounds_clear(From(p).sounds); };
        return s;
    }
};

#endif
