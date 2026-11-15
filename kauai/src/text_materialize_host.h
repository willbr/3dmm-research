#ifndef TEXT_MATERIALIZE_HOST_H
#define TEXT_MATERIALIZE_HOST_H
#include "mm/text_materialize.h"
// Native representations and virtual extension hooks for C materialization.
struct RichTextDocument::MaterializeHost : DocumentHost
{
    RichTextStoreHost::Host stored;
    explicit MaterializeHost(RichTextDocument *p, PChunkyFile file = nullptr) : DocumentHost(p), stored{file}
    {
    }
    void Rethrow()
    {
        stored.Rethrow();
        DocumentHost::Rethrow();
    }
    MMTextMaterializeServices Read()
    {
        MMTextMaterializeServices s = {};
        s.context = this;
        s.read = stored.Read();
        s.base = Base();
        s.init_base = [](void *v) -> int {
            auto &h = *static_cast<MaterializeHost *>(v);
            return h.Guard([&] { return h.rich->TextDocumentBase::_FInit(); });
        };
        s.defaults = [](void *v, const MMRichTextDefaults *p, const MMMovieTitle *font) -> int {
            auto &h = *static_cast<MaterializeHost *>(v);
            return h.Guard([&] {
                h.rich->_dxpDef = p->width;
                h.rich->_dypFontDef = p->height;
                h.rich->_acrBack.SetFromLw(p->background);
                RichTextStoreHost::Font(*font, &h.rich->_stnFontDef);
                h.rich->_oskFont = static_cast<short>(p->font_system);
                h.rich->_onnDef = vntl.OnnMapStn(&h.rich->_stnFontDef, h.rich->_oskFont);
                return 1;
            });
        };
        s.replace = [](void *v, const MMTextLocation *p, int copy, MMI32 count) -> int {
            auto &h = *static_cast<MaterializeHost *>(v);
            return h.Guard([&] {
                auto location = RichTextStoreHost::Location(*p);
                return h.rich->_pbsf->FReplaceFlo(&location, copy != 0, 0, count);
            });
        };
        s.count = [](void *, MMU32 kind, void *p) -> MMI32 {
            return kind == MM_TEXT_PROPERTIES ? static_cast<PDynamicArray>(p)->IvMac()
                                              : static_cast<PAllocatedGroup>(p)->IvMac();
        };
        s.properties = [](void *, void *p) -> void * { return static_cast<PDynamicArray>(p)->QvGet(0); };
        s.free_argument = [](void *, void *p, MMI32 i) -> int { return static_cast<PAllocatedGroup>(p)->FFree(i); };
        s.fixed = [](void *, void *p, MMI32 i) -> MMU32 {
            LegacyLong fixed;
            static_cast<PAllocatedGroup>(p)->GetFixed(i, &fixed);
            return static_cast<MMU32>(fixed);
        };
        s.put_fixed = [](void *, void *p, MMI32 i, MMU32 value) {
            LegacyLong fixed = static_cast<LegacyLong>(value);
            static_cast<PAllocatedGroup>(p)->PutFixed(i, &fixed);
        };
        s.open_argument = [](void *v, MMI32 i, unsigned kind, int order, int system) -> int {
            auto &h = *static_cast<MaterializeHost *>(v);
            return h.Guard([&] {
                return h.rich->_FOpenArg(i, static_cast<byte>(kind), static_cast<short>(order),
                                         static_cast<short>(system));
            });
        };
        return s;
    }
    MMTextFontArgumentServices Argument()
    {
        MMTextFontArgumentServices s = {};
        s.context = this;
        s.bytes = [](void *v, MMI32 i) -> MMI32 { return static_cast<MaterializeHost *>(v)->rich->_pagcact->Cb(i); };
        s.system = [](void *v, MMI32 i) -> int {
            short system;
            static_cast<MaterializeHost *>(v)->rich->_pagcact->GetRgb(i, 4, 2, &system);
            return system;
        };
        s.lock = [](void *v, MMI32 i) -> const void * {
            return static_cast<MaterializeHost *>(v)->rich->_pagcact->PvLock(i);
        };
        s.unlock = [](void *v) { static_cast<MaterializeHost *>(v)->rich->_pagcact->Unlock(); };
        s.decode = [](void *v, const void *p, MMI32 bytes, MMMovieTitle *out) -> int {
            auto &h = *static_cast<MaterializeHost *>(v);
            return h.Guard([&] {
                String name;
                if (!name.FSetData(const_cast<void *>(p), bytes))
                    return false;
                *out = RichTextStoreHost::Font(name);
                return true;
            });
        };
        s.map = [](void *v, const MMMovieTitle *p, int system, MMI32 *out) -> int {
            auto &h = *static_cast<MaterializeHost *>(v);
            return h.Guard([&] {
                String name;
                RichTextStoreHost::Font(*p, &name);
                *out = vntl.OnnMapStn(&name, static_cast<short>(system));
                return 1;
            });
        };
        s.put = [](void *v, MMI32 i, MMI32 value) {
            LegacyLong font = value;
            static_cast<MaterializeHost *>(v)->rich->_pagcact->PutRgb(i, 0, 4, &font);
        };
        s.diagnostic = [](void *) { Bug("bad font entry"); };
        return s;
    }
};
#endif
