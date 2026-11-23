/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "bren_core.h"
#include "background_renderer.h"
#include "handle_services.h"

using namespace BRender;

ASSERTNAME

const MMBRenderBackgroundHost *mm_kauai_background_host(void)
{
    static const auto teardown = [] {
        MMBackgroundServices s = {};
        mm_brender_background_light_services(&s);
        s.debug_checks = DebugShip(1, 0);
        s.allocate = [](void *, MMI32 bytes, void **out) -> int { return FAllocPv(out, bytes, fmemClear, mprNormal); };
        s.free_memory = [](void *, void **out) { FreePpv(out); };
        s.diagnostic = [](void *, int issue) {
            if (issue == MM_BACKGROUND_CAMERA_GAPS)
                Bug("cam chids are not consecutive!");
            else
                Warn("Error reading background");
        };
        return s;
    }();
    // Preserve the cache charge of the former native background class.
    constexpr size_t prefix = sizeof(void *) == 8 ? 40 : 28 + DebugShip(4, 0);
    constexpr size_t bytes = prefix + sizeof(MMBackgroundResource) + sizeof(MMBackgroundCameraState) + sizeof(String);
    constexpr auto cacheBytes = static_cast<MMI32>((bytes + sizeof(void *) - 1) & ~(sizeof(void *) - 1));
    static const MMBRenderBackgroundHost host = {
        {KauaiResourceObjectAllocator(), &teardown, sizeof(achar),
#ifdef DEBUG
         mm_kauai_background_mark
#else
         nullptr
#endif
        },
        nullptr, cacheBytes,
        [](void **object, MMI32 *bytes) {
            TrashVar(object);
            TrashVar(bytes);
        }
    };
    return &host;
}

void mm_kauai_background_validate(MMBackgroundOwner *owner)
{
#ifdef DEBUG
    if (!owner)
        return;
    const auto &resource = *mm_background_owner_resource(owner);
    const auto &camera = *mm_background_owner_camera(owner);
    AssertIn(resource.light_count, 1, 100);
    AssertIn(camera.count, 1, 100);
    Assert(camera.current == ivNil || (camera.current >= 0 && camera.current < camera.count),
           "bad Camera(background).current");
    AssertPvCb(resource.actors, LwMul(resource.light_count, size(BACT)));
    AssertPvCb(resource.lights, LwMul(resource.light_count, size(BLIT)));
#else
    (void)owner;
#endif
}

void mm_kauai_background_mark(MMBackgroundOwner *owner)
{
#ifdef DEBUG
    if (!owner)
        return;
    mm_kauai_background_validate(owner);
    MarkPv(owner);
    const auto binding = mm_background_owner_binding(owner);
    if (binding->cache && !binding->attached)
        mm_resource_cache_mark(binding->cache);
    const auto &resource = *mm_background_owner_resource(owner);
    MarkPv(resource.actors);
    MarkPv(resource.lights);
#else
    (void)owner;
#endif
}

void mm_kauai_background_camera_diagnostic(void *, int issue)
{
    if (issue == MM_BACKGROUND_CAMERA_SHORT)
        Bug("CameraPosition chunk not large enough");
    else
        Bug("CameraPosition chunk's extra data not an even multiple of size(APOS)");
}
