/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME

ActorTrack::~ActorTrack()
{
    Clear();
}

void ActorTrack::Clear()
{
    CloseTags();
    ReleasePpo(&events);
    ReleasePpo(&route);
}

void ActorTrack::CloseTags()
{
    if (events == pvNil)
        return;
    events->Lock();
    for (LegacyLong index = 0; index < events->IvMac(); ++index)
    {
        PTAG tag;
        if (FEventTag(events, index, &tag))
        {
            TAG aligned;
            CopyPb(tag, &aligned, size(TAG));
            if (aligned.sid == ksidUseCrf && aligned.pcrf != pvNil)
            {
                TagManager::CloseTag(&aligned);
                CopyPb(&aligned, tag, size(TAG));
            }
        }
    }
    events->Unlock();
}

bool ActorTrack::FEventTag(PGeneralGroup events, LegacyLong index, PTAG *tag, ActorEvent::PBase *event)
{
    auto base = (ActorEvent::PBase)events->QvFixedGet(index);
    if (event != pvNil)
        *event = base;
    *tag = pvNil;
    if (base->aet == ActorEvent::aetCost)
    {
        ActorEvent::Costume costume;
        events->Get(index, &costume);
        if (!costume.fCmtl)
            *tag =
                reinterpret_cast<PTAG>(static_cast<byte *>(events->QvGet(index)) + offsetof(ActorEvent::Costume, tag));
    }
    else if (base->aet == ActorEvent::aetSnd)
        *tag = reinterpret_cast<PTAG>(static_cast<byte *>(events->QvGet(index)) + offsetof(ActorEvent::Sound, tag));
    return *tag != pvNil;
}

void ActorTrack::Swap(ActorTrack &other)
{
    SwapVars(&events, &other.events);
    SwapVars(&route, &other.route);
}

bool ActorTrack::FCreate()
{
    ActorTrack created;
    created.events = GeneralGroup::PggNew(sizeof(ActorEvent::Base), 10, sizeof(ActorEvent::Add));
    if (created.events == pvNil)
        return fFalse;
    created.route = DynamicArray::PglNew(sizeof(RouteDistancePoint), 32);
    if (created.route == pvNil)
        return fFalse;
    created.route->SetMinGrow(32);
    Swap(created);
    return fTrue;
}

bool ActorTrack::FCloneFrom(const ActorTrack &source)
{
    if (this == &source)
        return fTrue;
    ActorTrack clone;
    // Copy route first: once event bytes are copied, acquire their references
    // immediately, before any operation that can fail.
    if (source.route != pvNil && (clone.route = source.route->PglDup()) == pvNil)
        return fFalse;
    if (source.events != pvNil)
    {
        clone.events = source.events->PggDup();
        if (clone.events == pvNil)
            return fFalse;
        clone.events->Lock();
        for (LegacyLong index = 0; index < clone.events->IvMac(); ++index)
        {
            PTAG tag;
            if (FEventTag(clone.events, index, &tag))
            {
                TAG aligned;
                CopyPb(tag, &aligned, size(TAG));
                if (aligned.sid == ksidUseCrf && aligned.pcrf != pvNil)
                    TagManager::DupTag(&aligned);
            }
        }
        clone.events->Unlock();
    }
    Swap(clone);
    return fTrue;
}
