/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_movie_graph.h"
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
MMActorHeaderIO ActorData::HeaderIO(PChunkyFile file)
{
    return {file,
            [](void *c, MMU32 number, MMReadReceiver receive, void *out) -> int {
                return EngineMovieGraph::ReceivePayload(static_cast<PChunkyFile>(c), kctgActr, number, receive, out);
            },
            [](void *c, MMU32 number, const void *header, MMU32 bytes) -> int {
                return static_cast<PChunkyFile>(c)->FPutPv(const_cast<void *>(header), bytes, kctgActr, number);
            }};
}
namespace
{
const MMActorRouteReader kRouteReader = {nullptr,
    [](void *, MMU32 count, void **rows) -> void * {
        const auto allocator = CollectionAllocator();
        auto path = mm_actor_path_create(&allocator, 0, DebugShip(1, 0));
        if (!path)
            return nullptr;
        if (mm_array_set_count(&path->rows, count) != MM_STORAGE_OK)
        {
            mm_actor_path_release(path);
            return nullptr;
        }
        *rows = path->storage.buffers[0].data;
        return path;
    },
    [](void *p) { mm_actor_path_release(static_cast<MMActorPath *>(p)); }};
} // namespace
MMActorEventServices ActorData::EventServices()
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
bool ActorData::FReadHeader(PDataBlock block, ActorChunkOnFile *header)
{
    AssertPo(block, 0);
    AssertVarMem(header);
    if (!block->FUnpackData())
        return false;
    const auto stream = EngineReadStream::FromBlock(*block);
    return mm_actor_read_header(&stream, header) != 0;
}
bool ActorData::FReadHeader(PChunkyFile file, ChunkNumber actor, void *header)
{
    const auto graph = EngineMovieGraph::FromFile(file);
    return mm_movie_actor_header(&graph, actor, header) == MM_MOVIE_OK;
}
bool ActorData::FWriteHeader(PChunkyFile file, ChunkNumber number, ActorChunkOnFile *header)
{
    const auto io = HeaderIO(file);
    return mm_actor_write_header(number, header, &io) != 0;
}
bool ActorData::FAdjustId(PChunkyFile file, ChunkNumber number, LegacyLong delta)
{
    const auto io = HeaderIO(file);
    return mm_actor_adjust_id(number, delta, &io) != 0;
}
MMActorPath *ActorData::PpathRead(PDataBlock block)
{
    if (!block->FUnpackData())
        return nullptr;
    const auto stream = EngineReadStream::FromBlock(*block);
    void *route;
    if (mm_actor_read_route_stream(&stream, &kRouteReader, &route) != MM_MOVIE_OK)
        return nullptr;
    return static_cast<MMActorPath *>(route);
}
MMActorPath *ActorData::PpathRead(PChunkyFile file, ChunkNumber actor)
{
    const auto graph = EngineMovieGraph::FromFile(file);
    void *route;
    if (mm_movie_actor_route(&graph, actor, &kRouteReader, &route) != MM_MOVIE_OK)
        return nullptr;
    return static_cast<MMActorPath *>(route);
}
bool ActorData::FWritePath(MMActorPath *path, PDataBlock block)
{
    const MMStorageWriter writer = {
        block, static_cast<MMU32>(block->Cb()),
        [](void *p, MMU32 offset, const void *data, size_t bytes) -> int {
            return static_cast<PDataBlock>(p)->FWriteRgb(const_cast<void *>(data), bytes, offset);
        }};
    return mm_actor_path_write(path, koskCur, &writer) != 0;
}
PGeneralGroup ActorData::PggReadEvents(PDataBlock block)
{
    if (!block->FUnpackData())
        return nullptr;
    const auto stream = EngineReadStream::FromBlock(*block);
    const auto services = EventServices();
    void *events;
    if (mm_actor_read_events_stream(&stream, &services, &events) != MM_MOVIE_OK)
        return nullptr;
    return static_cast<PGeneralGroup>(events);
}
PGeneralGroup ActorData::PggReadEvents(PChunkyFile file, ChunkNumber actor)
{
    const auto graph = EngineMovieGraph::FromFile(file);
    const auto services = EventServices();
    void *events;
    if (mm_movie_actor_events(&graph, actor, &services, &events) != MM_MOVIE_OK)
        return nullptr;
    return static_cast<PGeneralGroup>(events);
}
PGeneralGroup ActorData::PggWriteEvents(PGeneralGroup events)
{
    const auto services = EventServices();
    return static_cast<PGeneralGroup>(mm_actor_write_events(events, &services));
}
