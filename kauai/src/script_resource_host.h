#ifndef KAUAI_SCRIPT_RESOURCE_HOST_H
#define KAUAI_SCRIPT_RESOURCE_HOST_H
#include "mm/script_resource.h"
#include "handle_services.h"

static inline const MMScriptHost &ScriptHost()
{
    static const MMScriptHost host = {
        nullptr, KauaiDecodeBlockHandle, KauaiResourceObjectAllocator(), KauaiCopyAllocator(), koskCur, sizeof(achar),
        [](void *, const void *input, size_t bytes, int encoding, void *out, size_t capacity) -> size_t {
            return CchTranslateRgb(const_cast<void *>(input), bytes, encoding, static_cast<achar *>(out), capacity);
        },
        [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); },
#ifdef DEBUG
        MarkPv
#else
        nullptr
#endif
    };
    return host;
}
#endif
