#ifndef MM_PREVIEW_COSTUME_HOST_H
#define MM_PREVIEW_COSTUME_HOST_H
// Native resource/group adapters for the shared retail costume editor.
struct PreviewActor::CostumeHost : PreviewActor::Host
{
    explicit CostumeHost(PreviewActor *a) : Host{a} {}
    MMActorCostumeEditServices Services()
    {
        MMActorCostumeEditServices s = {};
        s.context = this;
        auto &e = s.edits;
        e.context = this;
        e.close_event_tag = [](void *v, MMI32 i) {
            auto g = static_cast<CostumeHost *>(v)->actor->track.Events(); PTAG tag;
            g->Lock(); if (ActorTrack::FEventTag(g, i, &tag)) TagManager::CloseTag(tag); g->Unlock();
        };
        e.duplicate_event_tag = [](void *v, MMI32 i) {
            auto g = static_cast<CostumeHost *>(v)->actor->track.Events(); PTAG tag;
            g->Lock(); if (ActorTrack::FEventTag(g, i, &tag)) TagManager::DupTag(tag); g->Unlock();
        };
        e.delete_event = [](void *v, MMI32 i) { static_cast<CostumeHost *>(v)->actor->track.Events()->Delete(i); };
        e.scene_frame = [](void *v, MMI32 *f) { *f = static_cast<CostumeHost *>(v)->actor->core.state.frame; return 1; };
        e.update_sound_frame = [](void *) {};
        e.insert_event = [](void *v, MMI32 i, MMU32 bytes, const void *p, const MMActorEvent *event) -> int {
            auto &h = *static_cast<CostumeHost *>(v);
            return h.Guard([&] { ActorEvent::Base value; std::memcpy(&value, event, sizeof(value));
                return int(h.actor->track.Events()->FInsert(i, bytes, const_cast<void *>(p), &value)); }) > 0;
        };
        e.write_payload = [](void *v, MMI32 i, const void *p) {
            static_cast<CostumeHost *>(v)->actor->track.Events()->Put(i, const_cast<void *>(p));
        };
        e.copy_payload = [](void *v, MMI32 dst, MMI32 src) {
            auto g = static_cast<CostumeHost *>(v)->actor->track.Events(); g->Put(dst, g->QvGet(src));
        };
        e.apply_event = [](void *v, MMActorState *state, MMI32 i) -> int {
            auto &h = *static_cast<CostumeHost *>(v);
            return h.Guard([&] { return int(h.actor->ApplyEvent(*state, h.actor->track, i, false)); }) > 0;
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
        s.reserve_events = [](void *v, MMU32 entries, MMU32 bytes) -> int {
            auto &h = *static_cast<CostumeHost *>(v);
            return h.Guard([&] { return int(h.actor->track.Events()->FEnsureSpace(entries, bytes, fgrpNil)); }) > 0;
        };
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
