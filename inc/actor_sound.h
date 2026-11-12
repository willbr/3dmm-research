/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef ACTOR_SOUND_H
#define ACTOR_SOUND_H
#include "actor_data.h"
#include "three_mm/actor_sound.h"
// Temporary native value facade. Storage, cloning and sound policy belong to C.
class ActorSoundList
{
    ThreeMMActorSounds _core;
  public:
    ActorSoundList();
    ~ActorSoundList();
    ActorSoundList(const ActorSoundList &) = delete;
    ActorSoundList &operator=(const ActorSoundList &) = delete;
    bool FCreate();
    bool FCloneFrom(const ActorSoundList &);
    void Swap(ActorSoundList &);
    void Clear();
    LegacyLong IvMac() const { return static_cast<LegacyLong>(_core.count); }
    void Get(LegacyLong, ActorEvent::SoundMotionMatch *) const;
    void Put(LegacyLong, const ActorEvent::SoundMotionMatch *);
    void Delete(LegacyLong);
    ThreeMMActorSounds *Core() { return &_core; }
    bool Valid() const;
};
#endif
