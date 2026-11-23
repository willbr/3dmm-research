/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    actrsave.cpp: Actor load/save code

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    Here's the chunk hierarchy:

    Actor // contains an ActorChunkOnFile (origin, arid, nfrmFirst, tagTmpl...)
     |
     +---PATH (chid 0) // _pglxyz (actor path)
     |
     +---GGAE (chid 0) // _owner.track.events (actor events)
     |
     +---MTRL children for embedded costume events


***************************************************************************/
#include "soc.h"
#include "mm/movie_session.h"
#include "mm/actor_persistence.h"
#include "mm/movie_sound.h"
#include "mm/scene_data.h"
ASSERTNAME

/***************************************************************************
    Write the actor out to disk.  Store the root chunk in the given ChunkNumber.
    If this function returns false, it is the client's responsibility to
    delete the actor chunks.
***************************************************************************/
bool Actor::FWrite(const MMArchiveWriter &archive, const MMMovieEmbeddedArchive &assets,
                   ChunkNumber cnoActr, ChunkNumber cnoScene)
{
    AssertThis(0);

    static_assert(ksidUseCrf == 0 && kctgActr == MM_ACTOR_CHUNK && kctgScen == MM_SCENE_CHUNK &&
                  kctgPath == MM_ACTOR_ROUTE_CHUNK && kctgGgae == MM_ACTOR_EVENTS_CHUNK &&
                  kctgMsnd == MM_SOUND_CHUNK, "actor archive constants");

    const auto timeline = _TimelineServices();
    MMActorStoreServices services = {};
    services.context = this;
    services.archive = &archive;
    services.assets = assets;
    services.timeline = &timeline;
#ifdef DEBUG
    services.missing_first_frame = [](void *) { Warn("Saving an actor who has no first frame number"); };
#endif
    return mm_actor_store(cnoActr, cnoScene, &_owner, &services) != 0;
}

/***************************************************************************
    Read the actor data from disk, (re-)construct the actor, and return a
    pointer to it.
***************************************************************************/
PActor Actor::PactrRead(MMResourceCache *pcrf, ChunkNumber cnoActr)
{
    AssertVarMem(pcrf);

    const auto graph = mm_movie_cache_graph(pcrf);
    if (!graph.access)
        return nullptr;
    const auto allocator = CollectionAllocator();
    MMActorLoadServices services = {};
    services.tags = &EngineActorTagServices();
    services.allocator = &allocator;
    services.trash_unused = DebugShip(1, 0);
    services.create = [](void *, MMActorOwner **owner) -> void * {
        auto actor = NewObj Actor;
        if (actor)
            *owner = &actor->_owner;
        return actor;
    };
    services.release = [](void *, void *p) { static_cast<PActor>(p)->Release(); };
    services.fetch_template = _FetchTemplate;
    services.timeline = [](void *, void *p) { return static_cast<PActor>(p)->_TimelineServices(); };
    services.diagnostic = [](void *, int diagnostic) {
        switch (diagnostic)
        {
        case MM_ACTOR_LOAD_TEMPLATE_LINK:
            Bug("where's the child Template?");
            break;
        case MM_ACTOR_LOAD_TEMPLATE_SOURCE:
            Assert(false, "Invalid template sid");
            break;
        case MM_ACTOR_LOAD_FAILED:
            Warn("PactrRead failed");
            break;
        }
    };
    auto actor = static_cast<PActor>(mm_actor_load(&graph, cnoActr, pcrf, &services));
    AssertNilOrPo(actor, 0);
    return actor;
}
