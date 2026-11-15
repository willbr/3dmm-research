#ifndef TEXT_PROPERTIES_HOST_H
#define TEXT_PROPERTIES_HOST_H
#include "mm/text_properties.h"
// Typed collections, cache slots and paragraph queries; C owns map/ref policy.
struct RichTextDocument::PropertyHost : DocumentHost
{
    const SPVM *changes = nullptr;
    explicit PropertyHost(RichTextDocument *p) : DocumentHost(p)
    {
    }
    static void Diagnostic(void *, int kind)
    {
        if (kind == MM_TEXT_ARG_INDEX)
        {
            Bug("bad index into AllocatedGroup");
        }
        else if (kind == MM_TEXT_ARG_REFS)
        {
            Assert(false, "bad AllocatedGroup reference count");
        }
        else if (kind == MM_TEXT_ARG_OVERFLOW)
        {
            Assert(false, "cact overflowed");
        }
        else
        {
            Assert(false, 0);
        }
    }
    MMTextArgumentServices Arguments()
    {
        MMTextArgumentServices s = {};
        s.context = this;
        s.objects = Objects();
        s.diagnostic = Diagnostic;
        s.create = [](void *v, MMI32 bytes, void **out) -> int {
            auto &h = *static_cast<PropertyHost *>(v);
            return h.Guard([&] {
                *out = AllocatedGroup::PagNew(sizeof(LegacyLong), 1, bytes);
                return *out != nullptr;
            });
        };
        s.count = [](void *, void *p) -> MMI32 { return static_cast<PAllocatedGroup>(p)->IvMac(); };
        s.free_slot = [](void *, void *p, MMI32 i) -> int { return static_cast<PAllocatedGroup>(p)->FFree(i); };
        s.data = [](void *, void *p, MMI32 i, MMI32 *bytes) -> const void * {
            LegacyLong count;
            auto data = static_cast<PAllocatedGroup>(p)->QvGet(i, &count);
            *bytes = count;
            return data;
        };
        s.fixed = [](void *, void *p, MMI32 i) -> MMU32 {
            LegacyLong fixed;
            static_cast<PAllocatedGroup>(p)->GetFixed(i, &fixed);
            return static_cast<MMU32>(fixed);
        };
        s.put_fixed = [](void *, void *p, MMI32 i, MMU32 value) {
            LegacyLong fixed = static_cast<LegacyLong>(value);
            static_cast<PAllocatedGroup>(p)->PutFixed(i, &fixed);
        };
        s.add = [](void *v, void *p, const void *bytes, MMI32 count, MMU32 value, MMI32 *out) -> int {
            auto &h = *static_cast<PropertyHost *>(v);
            return h.Guard([&] {
                LegacyLong index, fixed = static_cast<LegacyLong>(value);
                if (!static_cast<PAllocatedGroup>(p)->FAdd(count, &index, const_cast<void *>(bytes), &fixed))
                    return false;
                *out = index;
                return true;
            });
        };
        s.remove = [](void *, void *p, MMI32 i) { static_cast<PAllocatedGroup>(p)->Delete(i); };
        return s;
    }
    MMTextPropertyServices Properties()
    {
        MMTextPropertyServices s = {};
        s.context = this;
        s.arguments = Arguments();
        s.diagnostic = Diagnostic;
        s.count = [](void *v) -> MMI32 { return static_cast<PropertyHost *>(v)->rich->_pglmpe->IvMac(); };
        s.get = [](void *v, MMI32 i, MMTextPropertyEntry *out) {
            MPE p;
            static_cast<PropertyHost *>(v)->rich->_pglmpe->Get(i, &p);
            *out = {p.spcp, p.lw};
        };
        s.put = [](void *v, MMI32 i, const MMTextPropertyEntry *p) {
            MPE value = {p->key, p->value};
            static_cast<PropertyHost *>(v)->rich->_pglmpe->Put(i, &value);
        };
        s.insert = [](void *v, MMI32 i, const MMTextPropertyEntry *p) -> int {
            auto &h = *static_cast<PropertyHost *>(v);
            return h.Guard([&] {
                MPE value = {p->key, p->value};
                return h.rich->_pglmpe->FInsert(i, &value);
            });
        };
        s.remove = [](void *v, MMI32 i) { static_cast<PropertyHost *>(v)->rich->_pglmpe->Delete(i); };
        s.clear_cache = [](void *v) {
            auto doc = static_cast<PropertyHost *>(v)->rich;
            doc->_cpMinChp = doc->_cpLimChp = doc->_cpMinPap = doc->_cpLimPap = 0;
        };
        s.change = [](void *v, MMI32 i, MMTextPropertyChange *out) {
            auto &p = static_cast<PropertyHost *>(v)->changes[i];
            *out = {p.sprm, static_cast<MMU32>(p.lw), static_cast<MMU32>(p.lwMask)};
        };
        s.paragraph_end = [](void *v, MMI32 cp, MMI32 *out) -> int {
            auto &h = *static_cast<PropertyHost *>(v);
            return h.Guard([&] {
                *out = h.rich->CpLimPara(cp);
                return 1;
            });
        };
        return s;
    }
};
#endif
