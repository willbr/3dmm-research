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
#include "mm/actor_data.h"
#include "mm/movie_sound.h"
#include "mm/scene_data.h"
#include <cstring>
ASSERTNAME

using namespace ActorEvent;

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

    if (knfrmInvalid != _nfrmFirst)
    {
        long first, last;
        if (!FGetLifetime(&first, &last))
            return fFalse;
    }
#ifdef DEBUG
    if (knfrmInvalid == _nfrmFirst)
        Warn("Saving an actor who has no first frame number");
#endif

    MMActorHeader header = {};
    header.order = kboCur;
    header.system = koskCur;
    std::memcpy(header.translation, &_dxyzFullRte, sizeof(header.translation));
    header.actor_id = _arid;
    header.first_frame = _nfrmFirst;
    header.last_frame = _nfrmLast;
    header.source_id = _tagTmpl.sid;
    header.template_type = _tagTmpl.ctg;
    header.template_number = _tagTmpl.cno;

    struct Host
    {
        PChunkyFile file;
        PTemplate tmpl;
    } host = {pcfl, _ptmpl};
    MovieEmbeddedArchive materials = {pcfl, nullptr};
    MMActorStoreServices services = {};
    services.context = &host;
    services.header = ActorData::HeaderIO(pcfl);
    const auto allocator = CollectionAllocator();
    MMActorEventFactory factory = {&allocator, DebugShip(1, 0)};
    services.events = mm_actor_events_services(&factory);
    services.materials = materials.Services();
    services.write_template = [](void *p, MMU32 type, MMU32 number) -> int {
        auto h = static_cast<Host *>(p);
        Assert(h->tmpl->FIsTdt(), "Expected an embedded ThreeDText template");
        ChunkNumber written;
        if (!static_cast<PThreeDText>(h->tmpl)->FWrite(h->file, type, &written))
            return 0;
        if (written != number)
            h->file->Move(type, written, type, number);
        return 1;
    };
    services.write_track = [](void *p, MMU32 actor, MMU32 type, void *track) -> int {
        auto file = static_cast<Host *>(p)->file;
        DataBlock block;
        ChunkNumber number;
        if (type == kctgPath)
        {
            auto path = static_cast<MMActorPath *>(track);
            return file->FAddChild(kctgActr, actor, 0, mm_actor_path_wire_size(path), type, &number, &block) &&
                   ActorData::FWritePath(path, &block);
        }
        auto events = static_cast<MMActorEventTable *>(track);
        return file->FAddChild(kctgActr, actor, 0, mm_actor_events_wire_size(events), type, &number, &block) &&
               ActorData::FWriteEvents(events, &block);
    };
    return mm_actor_store(cnoActr, cnoScene, &header, _track.Route(), _track.Events(), &services) != 0;
}

/***************************************************************************
    Read the actor data from disk, (re-)construct the actor, and return a
    pointer to it.
***************************************************************************/
PActor Actor::PactrRead(PChunkyResourceFile pcrf, ChunkNumber cnoActr)
{
    AssertPo(pcrf, 0);

    Actor *pactr;
    PChunkyFile pcfl = pcrf->Pcfl();

    pactr = NewObj Actor;
    if (pvNil == pactr)
        goto LFail;
    if (!pactr->_FReadActor(pcfl, cnoActr))
        goto LFail;
    if (!pactr->_track.AdoptRoute(ActorData::PpathRead(pcfl, cnoActr)))
        goto LFail;
    if (!pactr->_track.AdoptEvents(ActorData::PeventsRead(pcfl, cnoActr)))
        goto LFail;
    if (!pactr->_FOpenTags(pcrf))
        goto LFail;
    if (!pactr->_motionSounds.FCreate())
        goto LFail;

    // Now that the tags are open, fetch the Template
    pactr->_ptmpl = (PTemplate)vptagm->PbacoFetch(&pactr->_tagTmpl, Template::FReadTmpl);
    if (pvNil == pactr->_ptmpl)
        goto LFail;

    if (knfrmInvalid == pactr->_nfrmLast && knfrmInvalid != pactr->_nfrmFirst)
    {
        long nfrmFirst, nfrmLast;

        if (!pactr->FGetLifetime(&nfrmFirst, &nfrmLast))
            goto LFail;
    }

    AssertPo(pactr, 0);
    return pactr;
LFail:
    Warn("PactrRead failed");
    ReleasePpo(&pactr);
    return pvNil;
}

/***************************************************************************
    Read the ActorChunkOnFile. This handles converting an ActorChunkOnFile that doesn't have an
    nfrmLast.
***************************************************************************/
/***************************************************************************
    Read the Actor chunk
***************************************************************************/
bool Actor::_FReadActor(PChunkyFile pcfl, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    ActorChunkOnFile actf;
    if (!ActorData::FReadHeader(pcfl, cno, &actf))
        return fFalse;

    Assert(kboCur == actf.bo, "bad ActorChunkOnFile");
    _dxyzFullRte = actf.dxyzFullRte;
    _arid = actf.arid;
    _nfrmFirst = actf.nfrmFirst;
    _nfrmLast = actf.nfrmLast;
    TagFromOnFile(&_tagTmpl, actf.tagTmpl);
    _fLifeDirty = (knfrmInvalid == _nfrmFirst) || (knfrmInvalid == _nfrmLast);

    if (_tagTmpl.sid == ksidUseCrf)
    {
        // Actor is a ThreeDText.  Tag might be wrong if this actor was imported,
        // so look for child Template.
        const auto chunks = pcfl->ReadChunkAccess();
        if (mm_actor_template_link(&chunks, cno, _tagTmpl.sid, 0, &_tagTmpl.cno) != MM_CHUNK_OK)
        {
            Bug("where's the child Template?");
            return fTrue; // hope the tag is correct
        }
    }

    return fTrue;
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

/***************************************************************************
    Marshal a wire-format pggaev into a fresh runtime pggaev. See header
    comment in actor.h for ownership rules.
***************************************************************************/
/***************************************************************************
    SwapBytes all events in pggaev
***************************************************************************/
/***************************************************************************
    Open all tags for this actor
***************************************************************************/
bool Actor::_FOpenTags(PChunkyResourceFile pcrf)
{
    AssertBaseThis(0);
    AssertPo(pcrf, 0);

    long iaev = 0;
    PTAG ptag;

    if (!TagManager::FOpenTag(&_tagTmpl, pcrf))
        return fFalse;

    mm_actor_events_lock(_track.Events());
    for (iaev = 0; iaev < mm_actor_events_count(_track.Events()); iaev++)
    {
        if (_FIsIaevTag(_track.Events(), iaev, &ptag))
        {
            if (!TagManager::FOpenTag(ptag, pcrf))
                goto LFail;
        }
    }
    mm_actor_events_unlock(_track.Events());
    return fTrue;
LFail:
    // Close the tags that were opened before failure
    while (--iaev >= 0)
    {
        if (_FIsIaevTag(_track.Events(), iaev, &ptag))
            TagManager::CloseTag(ptag);
    }
    mm_actor_events_unlock(_track.Events());
    return fFalse;
}

/***************************************************************************
    Close all tags in this actor's event stream
***************************************************************************/
void Actor::_CloseTags(void)
{
    AssertBaseThis(0); // because destructor calls this function

    TagManager::CloseTag(&_tagTmpl);
    _track.CloseTags();
}

/***************************************************************************
    If the iaev'th event of pggaev has a tag, sets *pptag to point to it.
    WARNING: unless you locked pggaev, *pptag is a qtag!
***************************************************************************/
bool Actor::_FIsIaevTag(MMActorEventTable *events, long iaev, PTAG *pptag, PBase *pqaev)
{
    return ActorTrack::FEventTag(events, iaev, pptag, pqaev);
}
