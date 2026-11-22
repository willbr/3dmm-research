/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kidspace_descriptor.h"
#include "handle_services.h"
ASSERTNAME

#ifdef DEBUG
void MarkKidspaceDescriptor(MMKidspaceOwner *owner)
{
    if (!owner)
        return;
    const auto descriptor = mm_kidspace_owner_descriptor(owner);
    Assert(descriptor->locations > 0 && descriptor->storage.buffers[0].data, "missing Kidspace descriptor data");
    MarkPv(owner);
    MarkPv(descriptor->storage.buffers[0].data);
    const auto binding = mm_kidspace_owner_binding(owner);
    if (binding->cache && !binding->attached)
        mm_resource_cache_mark(binding->cache);
}
#endif

static int ReadKidspaceDescriptor(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    static const MMKidspaceOwnerHost host = {
        KauaiResourceObjectAllocator(), KauaiCopyAllocator(), KauaiDecodeBlockHandle, nullptr,
#ifdef DEBUG
        MarkKidspaceDescriptor
#else
        nullptr
#endif
    };
    return mm_kidspace_owner_fetch(block, out, bytes, &host);
}

MMKidspaceOwner *FetchKidspaceDescriptor(Chunky::PResourceCache cache, ChunkTagOrType type, ChunkNumber number,
                                        bool *error)
{
    return static_cast<MMKidspaceOwner *>(
        cache->FetchOwned(type, number, ReadKidspaceDescriptor, mm_kidspace_owner_services(), error));
}
