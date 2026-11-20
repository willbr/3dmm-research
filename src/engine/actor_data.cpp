/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "engine_movie_graph.h"
#include "engine_read_stream.h"
#include "mm/actor_persistence.h"
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
                  offsetof(Costume, tag) == offsetof(MMActorCostume, tag) && offsetof(Costume, fCmtl) == 8,
              "actor costume payload ABI");
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
    const auto allocator = CollectionAllocator();
    const MMActorPathFactory factory = {&allocator, DebugShip(1, 0)};
    return mm_actor_path_read(&stream, &factory);
}
MMActorPath *ActorData::PpathRead(PChunkyFile file, ChunkNumber actor)
{
    const auto graph = EngineMovieGraph::FromFile(file);
    const auto allocator = CollectionAllocator();
    const MMActorPathFactory factory = {&allocator, DebugShip(1, 0)};
    MMActorPath *path;
    return mm_actor_movie_path(&graph, actor, &factory, &path) == MM_MOVIE_OK ? path : nullptr;
}
namespace
{
MMStorageWriter TrackWriter(PDataBlock block)
{
    return {
        block, static_cast<MMU32>(block->Cb()),
        [](void *p, MMU32 offset, const void *data, size_t bytes) -> int {
            return static_cast<PDataBlock>(p)->FWriteRgb(const_cast<void *>(data), bytes, offset);
        }};
}
} // namespace
bool ActorData::FWritePath(MMActorPath *path, PDataBlock block)
{
    const auto writer = TrackWriter(block);
    return mm_actor_path_write(path, koskCur, &writer) != 0;
}
MMActorEventTable *ActorData::PeventsRead(PDataBlock block)
{
    if (!block->FUnpackData())
        return nullptr;
    const auto stream = EngineReadStream::FromBlock(*block);
    const auto allocator = CollectionAllocator();
    MMActorEventFactory factory = {&allocator, DebugShip(1, 0)};
    const auto services = mm_actor_events_services(&factory);
    void *events;
    if (mm_actor_read_events_stream(&stream, &services, &events) != MM_MOVIE_OK)
        return nullptr;
    return static_cast<MMActorEventTable *>(events);
}
MMActorEventTable *ActorData::PeventsRead(PChunkyFile file, ChunkNumber actor)
{
    const auto graph = EngineMovieGraph::FromFile(file);
    const auto allocator = CollectionAllocator();
    MMActorEventFactory factory = {&allocator, DebugShip(1, 0)};
    MMActorEventTable *events;
    return mm_actor_movie_events(&graph, actor, &factory, &events) == MM_MOVIE_OK ? events : nullptr;
}
MMActorEventTable *ActorData::PeventsEncode(MMActorEventTable *events)
{
    const auto allocator = CollectionAllocator();
    MMActorEventFactory factory = {&allocator, DebugShip(1, 0)};
    const auto services = mm_actor_events_services(&factory);
    return static_cast<MMActorEventTable *>(mm_actor_write_events(events, &services));
}
bool ActorData::FWriteEvents(MMActorEventTable *events, PDataBlock block, short order)
{
    const auto writer = TrackWriter(block);
    return mm_actor_events_write(events, koskCur, order == kboOther, &writer) != 0;
}
