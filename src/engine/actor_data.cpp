/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_read_stream.h"
#include "mm/actor_data.h"
#include <cstddef>
ASSERTNAME
using namespace ActorEvent;
static_assert(sizeof(ActorChunkOnFile) == sizeof(MMActorHeader) && offsetof(ActorChunkOnFile, dxyzFullRte) == 4 &&
                  offsetof(ActorChunkOnFile, arid) == 16 && offsetof(ActorChunkOnFile, nfrmFirst) == 20 &&
                  offsetof(ActorChunkOnFile, nfrmLast) == 24 && offsetof(ActorChunkOnFile, tagTmpl) == 28,
              "actor header ABI");
static_assert(sizeof(TAG) == sizeof(MMTag) && offsetof(TAG, sid) == offsetof(MMTag, sid) &&
                  offsetof(TAG, pcrf) == offsetof(MMTag, cache) &&
                  offsetof(TAG, ctg) == offsetof(MMTag, type) &&
                  offsetof(TAG, cno) == offsetof(MMTag, number),
              "actor tag ABI");
static_assert(sizeof(tribool) == 4 && sizeof(Costume) == sizeof(MMActorCostume) &&
                  offsetof(Costume, tag) == offsetof(MMActorCostume, tag) && offsetof(Costume, fCmtl) == 8 &&
                  sizeof(Sound) == sizeof(MMActorSound) &&
                  offsetof(Sound, tag) == offsetof(MMActorSound, tag) && offsetof(Sound, chid) == 24,
              "actor runtime payload ABI");
static_assert(sizeof(Base) == 20 && sizeof(RouteDistancePoint) == 16 && sizeof(RoutePoint) == 12 &&
                  sizeof(BMAT34) == 48 && sizeof(Add) == 20 && offsetof(Add, xa) == 12 && offsetof(Add, ya) == 14 &&
                  offsetof(Add, za) == 16 && sizeof(Action) == 8 && sizeof(Stretch) == 12,
              "actor scalar payload ABI");
static_assert(aetAdd == MM_ACTOR_ADD && aetActn == MM_ACTOR_ACTION && aetCost == MM_ACTOR_COSTUME &&
                  aetRotF == MM_ACTOR_ROTATE_FORWARD && aetPull == MM_ACTOR_STRETCH &&
                  aetSize == MM_ACTOR_SIZE && aetSnd == MM_ACTOR_SOUND && aetMove == MM_ACTOR_MOVE &&
                  aetFreeze == MM_ACTOR_FREEZE && aetTweak == MM_ACTOR_TWEAK &&
                  aetStep == MM_ACTOR_STEP && aetRem == MM_ACTOR_REMOVE &&
                  aetRotH == MM_ACTOR_ROTATE_HERE,
              "actor event kinds");
namespace
{
int Order(short bo)
{
    return bo == kboCur ? MM_ACTOR_NATIVE : bo == kboOther ? MM_ACTOR_OTHER : MM_ACTOR_INVALID_ORDER;
}
struct HeaderHost
{
    PChunkyFile file;
    DataBlock block;
};
MMActorHeaderIO HeaderIO(HeaderHost *host)
{
    return {host,
            [](void *c, MMU32 number, MMReadStream *stream) -> int {
                auto h = static_cast<HeaderHost *>(c);
                if (!h->file->FFind(kctgActr, number, &h->block) || !h->block.FUnpackData())
                    return 0;
                *stream = EngineReadStream::FromBlock(h->block);
                return 1;
            },
            [](void *c, MMU32 number, const void *header, MMU32 bytes) -> int {
                return static_cast<HeaderHost *>(c)->file->FPutPv(const_cast<void *>(header), bytes, kctgActr, number);
            }};
}
MMActorEventServices Events()
{
    return {nullptr,
            [](void *g) -> MMU32 { return static_cast<PGeneralGroup>(g)->CbFixed(); },
            [](void *g) -> MMU32 { return static_cast<PGeneralGroup>(g)->IvMac(); },
            [](void *g, MMU32 i) -> void * { return static_cast<PGeneralGroup>(g)->QvFixedGet(i); },
            [](void *g, MMU32 i) -> MMI32 { return static_cast<PGeneralGroup>(g)->Cb(i); },
            [](void *g, MMU32 i) -> void * { return static_cast<PGeneralGroup>(g)->QvGet(i); },
            [](void *, MMU32 fixed) -> void * { return GeneralGroup::PggNew(fixed); },
            [](void *g, MMU32 i, MMU32 bytes, const void *variable, const void *fixed) -> int {
                return static_cast<PGeneralGroup>(g)->FInsert(i, bytes, const_cast<void *>(variable),
                                                              const_cast<void *>(fixed));
            },
            [](void *g) { static_cast<PGeneralGroup>(g)->Release(); }};
}
} // namespace
bool ActorData::FReadHeader(PDataBlock block, ActorChunkOnFile *header)
{
    AssertPo(block, 0);
    AssertVarMem(header);
    if (!block->FUnpackData())
        return false;
    const auto stream = EngineReadStream::FromBlock(*block);
    return mm_actor_read_header(&stream, header) != 0;
}
bool ActorData::FWriteHeader(PChunkyFile file, ChunkNumber number, ActorChunkOnFile *header)
{
    HeaderHost host = {file};
    const auto io = HeaderIO(&host);
    return mm_actor_write_header(number, header, &io) != 0;
}
bool ActorData::FAdjustId(PChunkyFile file, ChunkNumber number, LegacyLong delta)
{
    HeaderHost host = {file};
    const auto io = HeaderIO(&host);
    return mm_actor_adjust_id(number, delta, &io) != 0;
}
PDynamicArray ActorData::PglReadRoute(PDataBlock block)
{
    short bo = kboCur;
    auto route = DynamicArray::PglRead(block, &bo);
    const MMActorRouteServices services = {
        [](void *g) -> MMU32 { return static_cast<PDynamicArray>(g)->CbEntry(); },
        [](void *g) -> MMU32 { return static_cast<PDynamicArray>(g)->IvMac(); },
        [](void *g) -> void * { return static_cast<PDynamicArray>(g)->QvGet(0); },
        [](void *g) { static_cast<PDynamicArray>(g)->Release(); }};
    return static_cast<PDynamicArray>(mm_actor_finish_route(route, Order(bo), &services));
}
PGeneralGroup ActorData::PggReadEvents(PDataBlock block)
{
    short bo = kboCur;
    auto wire = GeneralGroup::PggRead(block, &bo);
    const auto services = Events();
    return static_cast<PGeneralGroup>(mm_actor_read_events(wire, Order(bo), &services));
}
PGeneralGroup ActorData::PggWriteEvents(PGeneralGroup events)
{
    const auto services = Events();
    return static_cast<PGeneralGroup>(mm_actor_write_events(events, &services));
}
