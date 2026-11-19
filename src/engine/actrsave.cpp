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
     +---GGAE (chid 0) // _track.Events() (actor events)
     |
     +---MTRL children for embedded costume events


***************************************************************************/
#include "soc.h"
#include "engine_movie_archive.h"
#include "engine_movie_graph.h"
#include "mm/actor_persistence.h"
#include "mm/movie_sound.h"
#include "mm/scene_data.h"
ASSERTNAME

/***************************************************************************
    Write the actor out to disk.  Store the root chunk in the given ChunkNumber.
    If this function returns false, it is the client's responsibility to
    delete the actor chunks.
***************************************************************************/
bool Actor::FWrite(PChunkyFile pcfl, ChunkNumber cnoActr, ChunkNumber cnoScene)
{
    AssertThis(0);
    AssertPo(pcfl, 0);

    static_assert(ksidUseCrf == 0 && kctgActr == MM_ACTOR_CHUNK && kctgScen == MM_SCENE_CHUNK &&
                  kctgPath == MM_ACTOR_ROUTE_CHUNK && kctgGgae == MM_ACTOR_EVENTS_CHUNK &&
                  kctgMsnd == MM_SOUND_CHUNK, "actor archive constants");

    struct Host
    {
        PActor actor;
        MovieArchiveHost archive;
    } host = {this, {pcfl}};
    MovieEmbeddedArchive materials = {pcfl, nullptr};
    MMActorStoreServices services = {};
    services.context = &host;
    services.header = ActorData::HeaderIO(pcfl);
    services.materials = materials.Services();
    services.system = koskCur;
    services.refresh_lifetime = [](void *p) -> int {
        long first, last;
        return static_cast<Host *>(p)->actor->FGetLifetime(&first, &last);
    };
#ifdef DEBUG
    services.missing_first_frame = [](void *) { Warn("Saving an actor who has no first frame number"); };
#endif
    services.write_template = [](void *p, void *object, MMU32 type, MMU32 number) -> int {
        auto file = static_cast<Host *>(p)->archive.file;
        auto tmpl = static_cast<PTemplate>(object);
        Assert(tmpl->FIsTdt(), "Expected an embedded ThreeDText template");
        ChunkNumber written;
        if (!static_cast<PThreeDText>(tmpl)->FWrite(file, type, &written))
            return 0;
        if (written != number)
            file->Move(type, written, type, number);
        return 1;
    };
    services.add_track = [](void *p, MMU32 actor, MMU32 type, MMU32 bytes, MMStorageWriter *writer) -> int {
        auto &archive = static_cast<Host *>(p)->archive;
        ChunkNumber number;
        if (!archive.file->FAddChild(kctgActr, actor, 0, bytes, type, &number, &archive.output))
            return 0;
        *writer = archive.OutputWriter(bytes);
        return 1;
    };
    return mm_actor_store(cnoActr, cnoScene, &_runtime, &_track.Core(), &services) != 0;
}

/***************************************************************************
    Read the actor data from disk, (re-)construct the actor, and return a
    pointer to it.
***************************************************************************/
PActor Actor::PactrRead(PChunkyResourceFile pcrf, ChunkNumber cnoActr)
{
    AssertPo(pcrf, 0);

    const auto graph = EngineMovieGraph::FromFile(pcrf->Pcfl());
    const auto allocator = CollectionAllocator();
    MMActorLoadServices services = {};
    services.tags = &ActorTrack::TagServices();
    services.allocator = &allocator;
    services.trash_unused = DebugShip(1, 0);
    services.create = [](void *, MMActorRuntime **runtime, MMActorTrack **track) -> void * {
        auto actor = NewObj Actor;
        if (actor)
        {
            *runtime = &actor->_runtime;
            *track = &actor->_track.Core();
        }
        return actor;
    };
    services.release = [](void *, void *p) { static_cast<PActor>(p)->Release(); };
    services.create_sounds = [](void *, void *p) -> int { return static_cast<PActor>(p)->_motionSounds.FCreate(); };
    services.fetch_template = _FetchTemplate;
    services.lifetime = [](void *, void *p) -> int {
        long first, last;
        return static_cast<PActor>(p)->FGetLifetime(&first, &last);
    };
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

/******************************************************************************
    FAdjustAridOnFile
        Given a chunky file, a ChunkNumber and a delta for the arid, updates the
        arid for the actor on file.

    Arguments:
        PChunkyFile pcfl   -- the file the actor's on
        ChunkNumber cno     -- the ChunkNumber of the actor
        long darid  -- the change of the arid

    Returns: fTrue if everything went well, fFalse otherwise

************************************************************ PETED ***********/
bool Actor::FAdjustAridOnFile(PChunkyFile pcfl, ChunkNumber cno, long darid)
{
    return ActorData::FAdjustId(pcfl, cno, darid);
}
