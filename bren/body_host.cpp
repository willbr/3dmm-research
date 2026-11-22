#include "kauai_core.h"
#include "body_renderer.h"
#include "handle_services.h"

ASSERTNAME

extern "C" const MMBRenderBody *mm_kauai_body_renderer(void)
{
    struct Binding
    {
        MMBRenderBody renderer;

        Binding()
        {
            const MMBRenderBodyHost host = {
                KauaiResourceObjectAllocator(), kcbMax,
                [](MMI32 bytes, void **out) -> int { return FAllocPv(out, bytes, fmemClear, mprNormal); },
                [](void **storage) { FreePpv(storage); },
                [](void **out) { TrashVar(out); },
                [](int code) {
                    switch (code)
                    {
                    case MM_BODY_MISSING_MATERIAL: Assert(false, "body set has no material"); break;
                    case MM_BODY_EMPTY_SET: Assert(false, "empty body set"); break;
                    case MM_BODY_SHORT_COSTUME: Assert(false, "not enough custom materials!"); break;
                    case MM_BODY_COSTUME_SHAPE: Assert(false, "different Body shapes!"); break;
                    }
                },
                [] { Bug("What actor is this?  It has no Body"); }
            };
            mm_brender_body_init(&renderer, &host);
        }
    };
    static const Binding binding;
    return &binding.renderer;
}
