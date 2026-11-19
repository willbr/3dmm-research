#ifndef MM_PREVIEW_COSTUME_HOST_H
#define MM_PREVIEW_COSTUME_HOST_H
// Native resource/group adapters for the shared retail costume editor.
struct PreviewActor::CostumeHost : PreviewActor::Host
{
    explicit CostumeHost(PreviewActor *a) : Host{a} {}
    MMActorEditServices EventEdits()
    {
        MMActorEditServices e = {};
        e.context = this;
        e.close_event_tag = [](void *v, MMI32 i) {
            static_cast<CostumeHost *>(v)->actor->track.CloseEventTag(i);
        };
        e.duplicate_event_tag = [](void *v, MMI32 i) {
            static_cast<CostumeHost *>(v)->actor->track.RetainEventTag(i);
        };
        e.delete_event = [](void *v, MMI32 i) { mm_actor_events_delete(static_cast<CostumeHost *>(v)->actor->track.Events(), i); };
        e.scene_frame = [](void *v, MMI32 *f) { *f = static_cast<CostumeHost *>(v)->actor->core.state.frame; return 1; };
        e.update_sound_frame = [](void *) {};
        e.insert_event = [](void *v, MMI32 i, MMU32 bytes, const void *p, const MMActorEvent *event) -> int {
            auto &h = *static_cast<CostumeHost *>(v);
            return h.Guard([&] { return mm_actor_events_insert(h.actor->track.Events(), i, bytes, p, event); }) > 0;
        };
        e.write_payload = [](void *v, MMI32 i, const void *p) {
            mm_actor_events_set_payload(static_cast<CostumeHost *>(v)->actor->track.Events(), i, p);
        };
        e.copy_payload = [](void *v, MMI32 dst, MMI32 src) {
            auto g = static_cast<CostumeHost *>(v)->actor->track.Events(); mm_actor_events_copy_payload(g, dst, src);
        };
        e.apply_event = [](void *v, MMActorState *state, MMI32 i) -> int {
            auto &h = *static_cast<CostumeHost *>(v);
            return h.Guard([&] { return int(h.actor->ApplyEvent(*state, h.actor->track.Core(), i, false,
                                                               &h.actor->motionSounds, &h.actor->sounds)); }) > 0;
        };
        e.dirty = [](void *) {};
        e.get_range = [](void *v, MMActorEditRange *r) {
            auto &p = static_cast<CostumeHost *>(v)->actor->core;
            r->first = p.header.first_frame; r->last = p.header.last_frame;
        };
        e.invalidate_range = [](void *) {};
        e.publish_range = [](void *v, const MMActorEditRange *r) {
            auto &p = static_cast<CostumeHost *>(v)->actor->core;
            p.header.first_frame = r->first; p.header.last_frame = r->last;
        };
        return e;
    }
    static int Reserve(void *v, MMU32 entries, MMU32 bytes)
    {
        auto &h = *static_cast<CostumeHost *>(v);
        return h.Guard([&] { return int(mm_actor_events_reserve(h.actor->track.Events(), entries, bytes)); }) > 0;
    }
    MMActorCostumeEditServices Services()
    {
        MMActorCostumeEditServices s = {};
        s.context = this;
        s.edits = EventEdits();
        s.reserve_events = Reserve;
        s.current = [](void *v, MMI32 set, int *custom) -> void * {
            bool plain; PMaterial_MTRL material; PCustomMaterial_CMTL costume;
            static_cast<CostumeHost *>(v)->actor->body->GetPartSetMaterial(set, &plain, &material, &costume);
            *custom = !plain; return plain ? static_cast<void *>(material) : static_cast<void *>(costume);
        };
        s.fetch_custom = [](void *v, MMI32 id) -> void * {
            auto &h = *static_cast<CostumeHost *>(v); void *out = nullptr;
            h.Guard([&] { out = h.actor->tmpl->PcmtlFetch(id); return 1; }); return out;
        };
        s.fetch_material = [](void *v, const MMTag *value) -> void * {
            auto &h = *static_cast<CostumeHost *>(v); void *out = nullptr;
            h.Guard([&] { TAGOnFile tag = {value->sid, 0, value->type, value->number};
                auto file = h.actor->resolve(tag);
                if (file) out = file->PbacoFetch(tag.ctg, tag.cno, Material_MTRL::FReadMtrl);
                return 1; }); return out;
        };
        s.body_set = [](void *, void *p) -> MMI32 { return static_cast<PCustomMaterial_CMTL>(p)->Ibset(); };
        s.release = [](void *, void *p) { static_cast<PBaseCacheableObject>(p)->Release(); };
        return s;
    }
};
#endif
