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
                  return *mm_text_collection_property_array(static_cast<RichTextDocument *>(v)->_properties, big);
              },
              [](void *v, const MMArray *rows) {
                  *mm_text_collection_property_array(static_cast<RichTextDocument *>(v)->_properties, nullptr) = *rows;
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
            [](void *p) { return *mm_text_collection_argument_group(static_cast<MMTextCollection *>(p)); },
            [](void *p, const MMGroup *rows) {
                *mm_text_collection_argument_group(static_cast<MMTextCollection *>(p)) = *rows;
            },
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
                const auto config = RichTextStoreHost::LiveCollectionConfig();
                auto owned = mm_text_collection_new(MM_TEXT_ARGUMENTS, &config);
                if (!owned)
                    return false;
                auto rows = mm_text_collection_argument_group(owned);
                rows->storage->min_grow[0] = 16 * rows->fixed_bytes;
                rows->storage->min_grow[1] = 16 * sizeof(MMGroupLocation);
                if (mm_group_reserve(rows, 1, bytes, 0) != MM_STORAGE_OK)
                {
                    mm_text_collection_dispose(owned);
                    return false;
                }
                *out = owned;
                return true;
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
