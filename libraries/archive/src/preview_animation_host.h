#ifndef MM_PREVIEW_ANIMATION_HOST_H
#define MM_PREVIEW_ANIMATION_HOST_H

struct PreviewActor::AnimationHost : PreviewActor::CostumeHost
{
    AnimationHost(PreviewActor *actor, const SoundGraphResolver &resolve) : CostumeHost(actor), resolveSound(resolve) {}
    const SoundGraphResolver &resolveSound;
    MMActorRecording record{};
    int lifeDirty = 0;
    MMSoundResource sound{};
    MMI32 silent = 0;
    unsigned char invalid = 0;

    static AnimationHost &Get(void *value)
    {
        return *static_cast<AnimationHost *>(static_cast<CostumeHost *>(value));
    }
    bool ReadSound(const MMTag &tag)
    {
        MMMovieSessionGraph graph;
        if (!resolveSound({tag.sid, 0, tag.type, tag.number}, graph))
            return false;
        MMSoundResourceServices s{};
        const unsigned short one = 1;
        sound = {}; silent = 0; invalid = 0;
        s.context = this;
        s.host_big_endian = *reinterpret_cast<const unsigned char *>(&one) == 0;
        s.graph = [](void *, void *f, MMMovieSessionGraph *out) {
            *out = *static_cast<const MMMovieSessionGraph *>(f);
        };
        s.read_name = [](void *, void *, MMU32, MMU32, void *) { return 1; };
        s.owner = [](void *v, void *, MMSoundOwner *out) {
            auto &h = *static_cast<AnimationHost *>(v);
            *out = {&h.sound, &h.silent, &h.invalid};
        };
        return mm_sound_read(this, &graph, tag.type, tag.number, &s) && !invalid;
    }
    MMActorActionEditServices Action()
    {
        MMActorActionEditServices s{};
        s.context = static_cast<CostumeHost *>(this);
        s.sounds.context = s.context;
        s.sounds.runtime.context = s.context;
        s.sounds.edits = EventEdits();
        s.sounds.runtime.cel_count = [](void *v, MMI32 action, MMI32 *out) -> int {
            auto &h = Get(v);
            return h.Guard([&] {
                LegacyLong count = 0;
                const auto ok = h.actor->Ptmpl()->FGetCcelActn(action, &count);
                *out = count;
                return ok;
            }) > 0;
        };
        s.sounds.default_sound = [](void *v, MMI32 action, MMI32 cel, int *exists, MMTag *out) -> int {
            auto &h = Get(v);
            return h.Guard([&] {
                TAG tag{}; bool available = false;
                const auto ok = h.actor->Ptmpl()->FGetSndActnCel(action, cel, &available, &tag);
                *exists = ok && available && tag.sid != 0;
                if (*exists) std::memcpy(out, &tag, sizeof(tag));
                return ok;
            }) > 0;
        };
        s.sounds.runtime.fetch = [](void *v, const MMTag *tag) -> void * {
            auto &h = Get(v); void *sound = nullptr;
            h.Guard([&] {
                if (h.ReadSound(*tag)) sound = &h;
                return 1;
            });
            return sound;
        };
        s.sounds.runtime.release = [](void *, void *) {};
        s.sounds.sound_info = [](void *, void *resource, MMActorSoundInfo *info) {
            const auto &h = *static_cast<AnimationHost *>(resource);
            *info = {h.sound.style, h.sound.volume, h.silent};
        };
        s.sounds.sounds_suppressed = [](void *) { return 1; };
        s.sounds.set_sound_frame = [](void *) {};
        s.sounds.midi_error = [](void *) {};
        s.action_flags = [](void *v, MMI32 action, MMU32 *flags) -> int {
            auto &h = Get(v);
            return h.Guard([&] {
                LegacyUlong flags32 = 0;
                const auto ok = h.actor->Ptmpl()->FGetGrfactn(action, &flags32);
                *flags = flags32;
                return ok;
            }) > 0;
        };
        s.text_template = [](void *v) { return int(Get(v).actor->Ptmpl()->FIsTdt()); };
        s.mark_life_dirty = [](void *v) { Get(v).lifeDirty = true; };
        return s;
    }
    MMActorSubrouteEditServices Subroute()
    {
        MMActorSubrouteEditServices s{};
        s.action = Action();
        s.route.edits = s.action.sounds.edits;
        s.timeline = Host::Services().timeline;
        mm_actor_route_edit_bind_tables(&s.route);
        s.route.life_dirty = s.action.mark_life_dirty;
        s.route.recording = [](void *v, int enabled) { Get(v).record.recording = enabled; };
        s.route.hide_body = [](void *) {};
        s.route.default_costume = [](void *v) {
            auto &h = Get(v);
            h.Guard([&] { return h.actor->Ptmpl()->FSetDefaultCost(h.actor->Pbody()); });
        };
        auto &header = actor->core.header;
        s.lifetime = {&header.first_frame, &header.last_frame, &lifeDirty, &record.recording, &record.rejoining, 0};
        s.pose_inputs = [](void *v, MMActorPoseInputs *inputs) {
            auto tmpl = Get(v).actor->Ptmpl();
            tmpl->GetRestOrien(&inputs->restX, &inputs->restY, &inputs->restZ);
            inputs->fixedPathOrientation = tmpl->FIsTdt();
        };
        s.locate_body = [](void *, const MMActorBodyPose *) {};
        s.seek = [](void *v, MMActorState *, MMI32 frame) -> int {
            auto &h = Get(v);
            return h.Guard([&] { return h.actor->Seek(frame); }) > 0;
        };
        return s;
    }
    MMActorRecordServices Record()
    {
        MMActorRecordServices s{};
        s.subroute = Subroute();
        s.distance = [](void *, const MMActorPoint *point) { return MMI32(BR_LENGTH3(point->dxr, point->dyr, point->dzr)); };
        s.publish_record = [](void *, const MMActorRecording *) {};
        s.restore = [](void *, void *, MMActorState *, MMActorRecording *) {}; // The caller discards its private draft.
        s.seek_scene = [](void *v, MMActorState *, MMActorRecording *, MMI32 frame) -> int {
            auto &h = Get(v);
            auto &actor = *h.actor;
            actor.core.header.last_frame = std::max(actor.core.header.last_frame, frame);
            return h.Guard([&] { return actor.Seek(frame) && actor.Visible(); }) > 0;
        };
        s.stationary_step = BrFloatToScalar(2);
        return s;
    }
};
#endif
