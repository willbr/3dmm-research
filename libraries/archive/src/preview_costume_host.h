#ifndef MM_PREVIEW_COSTUME_HOST_H
#define MM_PREVIEW_COSTUME_HOST_H
// Native resource and renderer hooks for the shared retail costume editor.
struct PreviewActor::CostumeHost : PreviewActor::Host
{
    explicit CostumeHost(PreviewActor *a) : Host{a} {}
    MMActorEditServices EventEdits()
    {
        MMActorEditServices e = {};
        e.context = this;
        e.tables = mm_actor_edit_tables(actor->Track().events, &EngineActorTagServices());
        e.scene_frame = [](void *v, MMI32 *f) { *f = static_cast<CostumeHost *>(v)->actor->core.state.frame; return 1; };
        e.update_sound_frame = [](void *) {};
        e.apply_event = [](void *v, MMActorState *state, MMI32 i) -> int {
            auto &h = *static_cast<CostumeHost *>(v);
            return h.Guard([&] { return int(h.actor->ApplyEvent(*state, h.actor->Track(), i, false,
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
    MMActorCostumeEditServices Services()
    {
        MMActorCostumeEditServices s = {};
        s.context = this;
        s.edits = EventEdits();
        s.current = [](void *v, MMI32 set, int *custom) -> void * {
            bool plain; PMaterial_MTRL material; PCustomMaterial_CMTL costume;
            static_cast<CostumeHost *>(v)->actor->Pbody()->GetPartSetMaterial(set, &plain, &material, &costume);
            *custom = !plain; return plain ? static_cast<void *>(material) : static_cast<void *>(costume);
        };
        s.fetch_custom = [](void *v, MMI32 id) -> void * {
            auto &h = *static_cast<CostumeHost *>(v); void *out = nullptr;
            h.Guard([&] { out = h.actor->Ptmpl()->PcmtlFetch(id); return 1; }); return out;
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
