/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    Actor Sound.

    Primary Author: ******
    Status:  Reviewed

***************************************************************************/

#include "soc.h"
#include <cstring>
#include "engine_actor_motion.h"

ASSERTNAME

using namespace ActorEvent;

/***************************************************************************

    Add a sound event to the actor event list, and create an undo object

***************************************************************************/
bool Actor::FSetSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fMotionMatch, long vlm, long sty)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertIn(sty, 0, styLim);

    PActor pactrDup;

    if (!FDup(&pactrDup))
    {
        return fFalse;
    }

    if (!FSetSndCore(ptag, fLoop, fQueue, fMotionMatch, vlm, sty))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    if (!FCreateUndo(pactrDup, fTrue))
    {
        Restore(pactrDup);
        ReleasePpo(&pactrDup);
        return fFalse;
    }

    ReleasePpo(&pactrDup);
    return fTrue;
}

/***************************************************************************

    Add a sound event to the actor event list
    For user sounds, the chid must also be fetched for the argument *ptag
    and stored with the event, since for user sounds, the cno can change
    (eg on scene import)
    Note: ptag is current because the sound browser has just completed building
    a current tag.

***************************************************************************/
ThreeMMActorSoundEditServices Actor::_SoundEditServices()
{
    ThreeMMActorSoundEditServices s = {};
    s.context = this;
    s.runtime = _SoundServices();
    s.edits = _EventEditServices();
    s.write_sound = s.runtime.write_event;
    s.dirty = [](void *p) { static_cast<Actor *>(p)->_pscen->MarkDirty(); };
    s.query_new = [](void *) -> void * {
        auto tags = DynamicArray::PglNew(sizeof(TAG), kctagSndGrow);
        if (tags) tags->SetMinGrow(kctagSndGrow);
        return tags;
    };
    s.query_add = [](void *, void *query, const ThreeMMTag *value) {
        TAG tag; std::memcpy(&tag, value, sizeof(tag));
        return int(static_cast<PDynamicArray>(query)->FAdd(&tag));
    };
    s.query_release = [](void *, void *query) { auto tags = static_cast<PDynamicArray>(query); ReleasePpo(&tags); };
    s.resolve_scene = [](void *p, ThreeMMTag *value, ThreeMMU32 child, ThreeMMU32 scene) {
        TAG tag; std::memcpy(&tag, value, sizeof(tag));
        bool ok = static_cast<Actor *>(p)->_pscen->Pmvie()->FResolveSndTag(&tag, child, scene);
        std::memcpy(value, &tag, sizeof(tag));
        return int(ok);
    };
    s.sound_info = [](void *, void *resource, ThreeMMActorSoundInfo *info) {
        auto sound = static_cast<PMovieSoundMSND>(resource);
        info->style = sound->Sty(); info->volume = sound->Vlm(); info->no_sound = sound->FNoSound();
    };
    s.reserve_events = [](void *p, ThreeMMU32 entries, ThreeMMU32 bytes) {
        return int(static_cast<Actor *>(p)->_track.Events()->FEnsureSpace(entries, bytes, fgrpNil));
    };
    s.sound_child = [](void *p, ThreeMMU32 number, ThreeMMU32 *child) {
        ChildChunkID value = *child;
        bool ok = static_cast<Actor *>(p)->_pscen->Pmvie()->FChidFromUserSndCno(number, &value);
        *child = value; return int(ok);
    };
    s.insert_apply = [](void *p, ThreeMMActorState *state, const ThreeMMActorSound *value) {
        auto actor = static_cast<Actor *>(p);
        Sound sound; std::memcpy(&sound, value, sizeof(sound));
        EngineActorMotion::Publish(actor->_state, *state);
        bool ok = actor->_FAddDoAev(aetSnd, sizeof(sound), &sound);
        *state = EngineActorMotion::Core(actor->_state);
        return int(ok);
    };
    s.sounds_suppressed = [](void *p) { return int((static_cast<Actor *>(p)->_pscen->GrfScen() & fscenSounds) != 0); };
    s.set_sound_frame = [](void *p) { static_cast<Actor *>(p)->Pscen()->Pmvie()->Pmcc()->SetSndFrame(fTrue); };
    s.midi_error = [](void *) { PushErc(ercSocNoActrMidi); };
    s.default_sound = [](void *p, ThreeMMI32 action, ThreeMMI32 cel, int *exists, ThreeMMTag *value) {
        TAG tag; bool found = false;
        bool ok = static_cast<Actor *>(p)->_ptmpl->FGetSndActnCel(action, cel, &found, &tag);
        *exists = found;
        if (ok && found) std::memcpy(value, &tag, sizeof(tag));
        return int(ok);
    };
    return s;
}
bool Actor::FSetSndCore(PTAG ptag, tribool fLoop, tribool fQueue, tribool fMotionMatch, long vlm, long sty)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertIn(sty, 0, styLim);
    static_assert(styMidi == 4 && styNil == 0 && vlmNil == -1, "sound style ABI");
    ThreeMMTag tag; std::memcpy(&tag, ptag, sizeof(tag));
    auto state = EngineActorMotion::Core(_state);
    const auto tables = EngineActorMotion::Services();
    const auto services = _SoundEditServices();
    ChildChunkID external = 0;
    TrashVar(&external);
    bool ok = three_mm_actor_sound_set(&state, _motionSounds.Core(), _track.Events(), _arid, &tag, fLoop, fQueue,
                                       fMotionMatch != 0, vlm, sty, external, &tables, &services) != 0;
    EngineActorMotion::Publish(_state, state);
    return ok;
}

/***************************************************************************

    Enqueue the sound at event iaev.
    Enter motion match sounds in the smm gl.
    Enter non-motion match sounds in the Msq

***************************************************************************/
ThreeMMActorSoundServices Actor::_SoundServices()
{
    ThreeMMActorSoundServices services = {};
    services.context = this;
    services.cel_count = [](void *context, ThreeMMI32 action, ThreeMMI32 *out) {
        LegacyLong count = *out;
        bool ok = static_cast<Actor *>(context)->_ptmpl->FGetCcelActn(action, &count);
        *out = count;
        return int(ok);
    };
    services.resolve = [](void *context, ThreeMMTag *value, ThreeMMU32 child) {
        TAG tag;
        std::memcpy(&tag, value, sizeof(tag));
        bool ok = static_cast<Actor *>(context)->_pscen->Pmvie()->FResolveSndTag(&tag, child);
        std::memcpy(value, &tag, sizeof(tag));
        return int(ok);
    };
    services.write_event = [](void *context, ThreeMMI32 index, const ThreeMMActorSound *value) {
        Sound sound;
        std::memcpy(&sound, value, sizeof(sound));
        static_cast<Actor *>(context)->_track.Events()->Put(index, &sound);
    };
    services.resolution_failed = [](void *) { Bug("Actrsnd: Expected to resolve snd tag"); };
    services.fetch = [](void *, const ThreeMMTag *value) -> void * {
        TAG tag;
        std::memcpy(&tag, value, sizeof(tag));
        return vptagm->PbacoFetch(&tag, MovieSoundMSND::FReadMsnd);
    };
    services.priority = [](void *, void *resource, int kind) -> ThreeMMI32 {
        const long tools[] = {toolSounder, toolLooper, toolMatcher};
        return static_cast<PMovieSoundMSND>(resource)->Spr(tools[kind]);
    };
    services.enqueue = [](void *context, void *resource, const ThreeMMActorSound *sound, ThreeMMI32 priority, int low) {
        auto actor = static_cast<Actor *>(context);
        return int(actor->_pscen->Pmvie()->Pmsq()->FEnqueue(static_cast<PMovieSoundMSND>(resource), actor->_arid,
                   sound->loop, sound->queue, sound->volume, priority, fTrue, 0, low != 0));
    };
    services.release = [](void *, void *resource) {
        auto sound = static_cast<PMovieSoundMSND>(resource);
        ReleasePpo(&sound);
    };
    return services;
}
bool Actor::_FEnqueueSnd(long iaev)
{
    AssertThis(0);
    AssertIn(iaev, 0, _track.Events()->IvMac());
    ThreeMMActorEvent event;
    ThreeMMActorSound sound;
    _track.Events()->GetFixed(iaev, &event);
    _track.Events()->Get(iaev, &sound);
    const auto services = _SoundServices();
    return three_mm_actor_sound_event(_motionSounds.Core(), &event, &sound, iaev, &services) != 0;
}
bool Actor::_FEnqueueSmmInMsq(void)
{
    AssertThis(0);
    const auto services = _SoundServices();
    return three_mm_actor_sound_motion(_motionSounds.Core(), _state.action, _state.cel, &services) != 0;
}

/***************************************************************************

    Delete old motion sounds in event list

***************************************************************************/
bool Actor::_FRemoveAevMm(long anid)
{
    AssertThis(0);
    auto state = EngineActorMotion::Core(_state);
    const auto tables = EngineActorMotion::Services();
    const auto services = _SoundEditServices();
    three_mm_actor_sound_remove_motion(&state, _track.Events(), anid, &tables, &services);
    EngineActorMotion::Publish(_state, state);
    return fTrue;
}

/***************************************************************************

    Insert default motion match sounds in event list

***************************************************************************/
bool Actor::_FAddAevDefMm(long anid)
{
    AssertThis(0);
    auto state = EngineActorMotion::Core(_state);
    const auto tables = EngineActorMotion::Services();
    const auto services = _SoundEditServices();
    ChildChunkID external = 0; TrashVar(&external);
    bool ok = three_mm_actor_sound_defaults(&state, _motionSounds.Core(), _track.Events(), _arid, anid, external,
                                            &tables, &services) != 0;
    EngineActorMotion::Publish(_state, state);
    return ok;
}

/***************************************************************************

    Set the Volume for sounds in the current frame of the specified sty

***************************************************************************/
bool Actor::FSetVlmSnd(long sty, bool fMotionMatch, long vlm)
{
    AssertThis(0); AssertIn(sty, 0, styLim); AssertIn(vlm, 0, kvlmFull + 1);
    auto state = EngineActorMotion::Core(_state);
    const auto tables = EngineActorMotion::Services();
    const auto services = _SoundEditServices();
    bool ok = three_mm_actor_sound_volume(&state, _motionSounds.Core(), _track.Events(), sty, fMotionMatch, vlm,
                                          &tables, &services) != 0;
    EngineActorMotion::Publish(_state, state);
    return ok;
}

/***************************************************************************

    Query actor for current frame sounds

***************************************************************************/
bool Actor::FQuerySnd(long sty, bool fMotionMatch, PDynamicArray *pglTagSnd, long *pvlm, bool *pfLoop)
{
    AssertThis(0); AssertIn(sty, 0, styLim); AssertVarMem(pvlm); AssertVarMem(pfLoop);
    struct Outputs { LegacyLong *volume; bool *loop; } outputs = {pvlm, pfLoop};
    ThreeMMActorSoundQuery result = {};
    result.context = &outputs;
    result.publish = [](void *p, ThreeMMI32 volume, int loop) {
        auto out = static_cast<Outputs *>(p); *out->volume = volume; *out->loop = loop != 0;
    };
    const auto state = EngineActorMotion::Core(_state);
    const auto tables = EngineActorMotion::Services();
    const auto services = _SoundEditServices();
    bool ok = three_mm_actor_sound_query(&state, _motionSounds.Core(), _track.Events(), sty, fMotionMatch,
                                         &result, &tables, &services) != 0;
    if (ok) *pglTagSnd = static_cast<PDynamicArray>(result.tags);
    return ok;
}

/***************************************************************************

    Delete a sound in this frame

***************************************************************************/
bool Actor::FDeleteSndCore(long sty, bool fMotionMatch)
{
    AssertThis(0); AssertIn(sty, 0, styLim);
    auto state = EngineActorMotion::Core(_state);
    const auto tables = EngineActorMotion::Services();
    const auto services = _SoundEditServices();
    bool ok = three_mm_actor_sound_delete(&state, _motionSounds.Core(), _track.Events(), sty, fMotionMatch,
                                          &tables, &services) != 0;
    EngineActorMotion::Publish(_state, state);
    return ok;
}

/******************************************************************************
    FSoundInFrm
        Enumerates all actor events for current frame looking for a sound
        event.

    Returns: fTrue if a sound event was found, fFalse otherwise

************************************************************ PETED ***********/
bool Actor::FSoundInFrm(void)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto state = EngineActorMotion::Core(_state);
    const auto tables = EngineActorMotion::Services();
    return three_mm_actor_sound_in_frame(&state, _track.Events(), Pscen()->Nfrm(), &tables) != 0;
}

/******************************************************************************
    Resolve all sound tags

******************************************************************************/
bool Actor::FResolveAllSndTags(ChunkNumber cnoScen)
{
    AssertThis(0);
    const auto tables = EngineActorMotion::Services();
    const auto services = _SoundEditServices();
    return three_mm_actor_sound_resolve_all(_track.Events(), cnoScen, &tables, &services) != 0;
}
