#ifndef MM_PREVIEW_ANIMATION_HOST_H
#define MM_PREVIEW_ANIMATION_HOST_H

struct PreviewActor::AnimationHost : PreviewActor::CostumeHost
{
    explicit AnimationHost(PreviewActor *actor) : CostumeHost(actor) {}
    MMActorRecording record{};
    bool lifeDirty = false;
    MMSoundResource sound{};
    MMI32 silent = 0;
    unsigned char invalid = 0;
    DataBlock soundBlock;

    static AnimationHost &Get(void *value)
    {
        return *static_cast<AnimationHost *>(static_cast<CostumeHost *>(value));
    }
    bool ReadSound(PChunkyFile file, const MMTag &tag)
    {
        MMSoundResourceServices s{};
        const unsigned short one = 1;
        sound = {}; silent = 0; invalid = 0;
        s.context = this;
        s.host_big_endian = *reinterpret_cast<const unsigned char *>(&one) == 0;
        s.find = [](void *v, void *f, MMU32 type, MMU32 number) -> void * {
            auto &block = static_cast<AnimationHost *>(v)->soundBlock;
            return static_cast<PChunkyFile>(f)->FFind(type, number, &block) ? &block : nullptr;
        };
        s.unpack = [](void *, void *p) { return int(static_cast<DataBlock *>(p)->FUnpackData()); };
        s.block_size = [](void *, void *p) { return MMI32(static_cast<DataBlock *>(p)->Cb()); };
        s.read = [](void *, void *p, void *out, MMI32 size) { return int(static_cast<DataBlock *>(p)->FReadRgb(out, size, 0)); };
        s.read_name = [](void *, void *, MMU32, MMU32, void *) { return 1; };
        s.owner = [](void *v, void *, MMSoundOwner *out) {
            auto &h = *static_cast<AnimationHost *>(v);
            *out = {&h.sound, &h.silent, &h.invalid};
        };
        s.child = [](void *, void *f, MMU32 type, MMU32 number, MMSoundChild *out) {
            ChildChunkIdentification child;
            if (!static_cast<PChunkyFile>(f)->FGetKidChid(type, number, 0, &child)) return 0;
            *out = {child.cki.ctg, child.cki.cno};
            return 1;
        };
        return mm_sound_read(this, file, tag.type, tag.number, &s) && !invalid;
    }
    MMActorActionEditServices Action()
    {
        MMActorActionEditServices s{};
        s.context = static_cast<CostumeHost *>(this);
        s.sounds.context = s.context;
        s.sounds.runtime.context = s.context;
        s.sounds.edits = EventEdits();
        s.sounds.reserve_events = Reserve;
        s.sounds.runtime.cel_count = [](void *v, MMI32 action, MMI32 *out) -> int {
            auto &h = Get(v);
            return h.Guard([&] {
                LegacyLong count = 0;
                const auto ok = h.actor->tmpl->FGetCcelActn(action, &count);
                *out = count;
                return ok;
            }) > 0;
        };
        s.sounds.default_sound = [](void *v, MMI32 action, MMI32 cel, int *exists, MMTag *out) -> int {
            auto &h = Get(v);
            return h.Guard([&] {
                TAG tag{}; bool available = false;
                const auto ok = h.actor->tmpl->FGetSndActnCel(action, cel, &available, &tag);
                *exists = ok && available && tag.sid != 0;
                if (*exists) std::memcpy(out, &tag, sizeof(tag));
                return ok;
            }) > 0;
        };
        s.sounds.runtime.fetch = [](void *v, const MMTag *tag) -> void * {
            auto &h = Get(v); void *sound = nullptr;
            h.Guard([&] {
                auto source = h.actor->resolve({tag->sid, 0, tag->type, tag->number});
                if (source && h.ReadSound(source->Pcfl(), *tag)) sound = &h;
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
                const auto ok = h.actor->tmpl->FGetGrfactn(action, &flags32);
                *flags = flags32;
                return ok;
            }) > 0;
        };
        s.text_template = [](void *v) { return int(Get(v).actor->tmpl->FIsTdt()); };
        s.mark_life_dirty = [](void *v) { Get(v).lifeDirty = true; };
        return s;
    }
    MMActorSubrouteEditServices Subroute()
    {
        MMActorSubrouteEditServices s{};
        s.action = Action();
        s.route.edits = s.action.sounds.edits;
        s.timeline = Host::Services().timeline;
        s.route.insert_point = [](void *p, MMI32 i, const MMActorRoutePoint *value) {
            try { return int(static_cast<PDynamicArray>(p)->FInsert(i, const_cast<MMActorRoutePoint *>(value))); }
            catch (...) { return 0; }
        };
        s.route.write_point = [](void *p, MMI32 i, const MMActorRoutePoint *value) {
            static_cast<PDynamicArray>(p)->Put(i, const_cast<MMActorRoutePoint *>(value));
        };
        s.route.delete_points = [](void *p, MMI32 i, MMI32 count) { static_cast<PDynamicArray>(p)->Delete(i, count); };
        s.route.clear_route = [](void *p) { static_cast<PDynamicArray>(p)->FSetIvMac(0); };
        s.route.write_event = s.timeline.put_event;
        s.route.swap_events = [](void *p, MMI32 a, MMI32 b) { static_cast<PGeneralGroup>(p)->Swap(a, b); };
        s.route.reserve_events = Reserve;
        s.route.life_dirty = s.action.mark_life_dirty;
        s.route.recording = [](void *v, int enabled) { Get(v).record.recording = enabled; };
        s.route.hide_body = [](void *) {};
        s.route.default_costume = [](void *v) {
            auto &h = Get(v);
            h.Guard([&] { return h.actor->tmpl->FSetDefaultCost(h.actor->body); });
        };
        s.get_flags = [](void *v, MMActorSubrouteFlags *flags) {
            const auto &h = Get(v);
            *flags = {h.lifeDirty, h.record.recording, h.record.rejoining};
        };
        s.set_dirty = [](void *v, int dirty) { Get(v).lifeDirty = dirty != 0; };
        s.pose_inputs = [](void *v, MMActorPoseInputs *inputs) {
            auto tmpl = Get(v).actor->tmpl;
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
        s.reserve_points = [](void *p, MMU32 count) {
            try { return int(static_cast<PDynamicArray>(p)->FEnsureSpace(count, fgrpNil)); }
            catch (...) { return 0; }
        };
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
