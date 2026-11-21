#ifndef TEXT_PROPERTIES_HOST_H
#define TEXT_PROPERTIES_HOST_H
#include "mm/text_properties.h"
// Typed collections, cache slots and paragraph queries; C owns map/ref policy.
struct RichTextDocument::PropertyHost : DocumentHost
{
    const SPVM *changes = nullptr;
    MMTextPropertyArray propertyArray;
    explicit PropertyHost(RichTextDocument *p)
        : DocumentHost(p), propertyArray{
              p,
              [](void *v, int *big) {
                  *big = RichTextStoreHost::NativeBig();
                  return static_cast<RichTextDocument *>(v)->_pglmpe->CoreRows();
              },
              [](void *v, const MMArray *rows) {
                  static_cast<RichTextDocument *>(v)->_pglmpe->PublishCoreRows(*rows);
              },
              [](void *) { Assert(false, "invalid property row mutation"); },
              0}
    {
    }
    int ReserveProperties(MMI32 count)
    {
        return mm_text_property_array_reserve(&propertyArray, count);
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
        static const MMTextLiveArgumentGroup group = {
            [](void *p) { return static_cast<PAllocatedGroup>(p)->CoreRows(); },
            [](void *p, const MMGroup *rows) { static_cast<PAllocatedGroup>(p)->PublishCoreRows(*rows); },
            [](void *) { Assert(false, "invalid group mutation"); }};
        MMTextArgumentServices s = {};
        s.context = this;
        s.objects = Objects();
        s.diagnostic = Diagnostic;
        s.validate_input = [](void *, unsigned, const void *p, MMI32 bytes) {
            AssertIn(bytes, 1, kcbMax);
            AssertPvCb(const_cast<void *>(p), bytes);
        };
        s.create = [](void *v, MMI32 bytes, void **out) -> int {
            auto &h = *static_cast<PropertyHost *>(v);
            return h.Guard([&] {
                *out = AllocatedGroup::PagNew(sizeof(LegacyLong), 1, bytes);
                return *out != nullptr;
            });
        };
        mm_text_live_argument_rows(&s.rows, &group);
        return s;
    }
    MMTextPropertyServices Properties()
    {
        MMTextPropertyServices s = {};
        s.context = this;
        s.arguments = Arguments();
        s.diagnostic = Diagnostic;
        mm_text_property_array_rows(&s.rows, &propertyArray);
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
