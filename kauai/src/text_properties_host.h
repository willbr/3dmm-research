#ifndef TEXT_PROPERTIES_HOST_H
#define TEXT_PROPERTIES_HOST_H
#include "mm/text_properties.h"
// Typed collections, cache slots and paragraph queries; C owns map/ref policy.
struct RichTextDocument::PropertyHost : DocumentHost
{
    const SPVM *changes = nullptr;
    MMTextPropertyBinding properties;
    explicit PropertyHost(RichTextDocument *p)
        : DocumentHost(p), properties{p->_properties, p, [](void *) { Assert(false, "invalid property row mutation"); }, 0}
    {
    }
    int ReserveProperties(MMI32 count)
    {
        return mm_text_collection_reserve_properties(properties.collection, count);
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
        else if (kind == MM_TEXT_ARG_MUTATION)
        {
            Assert(false, "invalid group mutation");
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
        s.validate_input = [](void *, unsigned, const void *p, MMI32 bytes) {
            AssertIn(bytes, 1, kcbMax);
            AssertPvCb(const_cast<void *>(p), bytes);
        };
        s.create = [](void *v, MMI32 bytes, void **out) -> int {
            auto &h = *static_cast<PropertyHost *>(v);
            return h.Guard([&] {
                const auto config = RichTextStoreHost::LiveCollectionConfig();
                *out = mm_text_collection_new_arguments(bytes, &config);
                return *out != nullptr;
            });
        };
        mm_text_collection_argument_rows(&s.rows);
        return s;
    }
    MMTextPropertyServices Properties()
    {
        MMTextPropertyServices s = {};
        s.context = this;
        s.arguments = Arguments();
        s.diagnostic = Diagnostic;
        mm_text_collection_property_rows(&s.rows, &properties);
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
