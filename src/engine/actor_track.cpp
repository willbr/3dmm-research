/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME
namespace
{
MMActorTrackServices Services()
{
    return {nullptr,
            [](void *, MMU32 fixed, MMU32 entries, MMU32 variable) -> void * {
                return GeneralGroup::PggNew(fixed, entries, variable);
            },
            [](void *, MMU32 width, MMU32 entries) -> void * { return DynamicArray::PglNew(width, entries); },
            [](void *g, MMU32 growth) { static_cast<PDynamicArray>(g)->SetMinGrow(growth); },
            [](void *g) -> void * { return static_cast<PGeneralGroup>(g)->PggDup(); },
            [](void *g) -> void * { return static_cast<PDynamicArray>(g)->PglDup(); },
            [](void *g) { static_cast<PGeneralGroup>(g)->Release(); },
            [](void *g) { static_cast<PDynamicArray>(g)->Release(); },
            [](void *g) { static_cast<PGeneralGroup>(g)->Lock(); },
            [](void *g) { static_cast<PGeneralGroup>(g)->Unlock(); },
            [](void *g) -> MMU32 { return static_cast<PGeneralGroup>(g)->IvMac(); },
            [](void *g, MMU32 i) -> void * { return static_cast<PGeneralGroup>(g)->QvFixedGet(i); },
            [](void *g, MMU32 i) -> void * { return static_cast<PGeneralGroup>(g)->QvGet(i); },
            [](void *cache) { static_cast<PChunkyResourceFile>(cache)->AddRef(); },
            [](void *cache) { static_cast<PChunkyResourceFile>(cache)->Release(); }};
}
} // namespace
ActorTrack::~ActorTrack()
{
    Clear();
}
void ActorTrack::Clear()
{
    const auto s = Services();
    mm_actor_track_dispose(&_storage, &s);
}
void ActorTrack::CloseTags()
{
    const auto s = Services();
    mm_actor_track_close_tags(&_storage, &s);
}
bool ActorTrack::FEventTag(PGeneralGroup events, LegacyLong index, PTAG *tag, ActorEvent::PBase *event)
{
    const auto s = Services();
    if (event)
        *event = static_cast<ActorEvent::PBase>(events->QvFixedGet(index));
    *tag = static_cast<PTAG>(mm_actor_event_tag(events, index, &s));
    return *tag != nullptr;
}
void ActorTrack::Swap(ActorTrack &other)
{
    mm_actor_track_swap(&_storage, &other._storage);
}
bool ActorTrack::FCreate()
{
    const auto s = Services();
    return mm_actor_track_create(&_storage, &s) != 0;
}
bool ActorTrack::FCloneFrom(const ActorTrack &source)
{
    const auto s = Services();
    return mm_actor_track_clone(&_storage, &source._storage, &s) != 0;
}
PGeneralGroup ActorTrack::AdoptEvents(PGeneralGroup events)
{
    const auto s = Services();
    mm_actor_track_adopt_events(&_storage, events, &s);
    return Events();
}
PDynamicArray ActorTrack::AdoptRoute(PDynamicArray route)
{
    const auto s = Services();
    mm_actor_track_adopt_route(&_storage, route, &s);
    return Route();
}
