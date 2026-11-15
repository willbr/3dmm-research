/* Actor motion-sound storage facade converted from actor.cpp and actrsnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "soc_core.h"
#include "actor_sound.h"
#include <cstring>
#include <cstddef>
ASSERTNAME
static_assert(sizeof(ActorEvent::SoundMotionMatch) == sizeof(MMActorMotionSound) &&
                  offsetof(ActorEvent::SoundMotionMatch, aevsnd) == offsetof(MMActorMotionSound, sound),
              "actor motion sound ABI");
ActorSoundList::ActorSoundList() { mm_actor_sounds_init(&_core, nullptr); }
ActorSoundList::~ActorSoundList() { mm_actor_sounds_dispose(&_core); }
bool ActorSoundList::FCreate() { return mm_actor_sounds_reserve(&_core, MM_ACTOR_SOUND_GROW) != 0; }
bool ActorSoundList::FCloneFrom(const ActorSoundList &source) { return mm_actor_sounds_clone(&_core, &source._core) != 0; }
void ActorSoundList::Swap(ActorSoundList &other) { mm_actor_sounds_swap(&_core, &other._core); }
void ActorSoundList::Clear() { mm_actor_sounds_clear(&_core); }
void ActorSoundList::Get(LegacyLong i, ActorEvent::SoundMotionMatch *out) const
{
    MMActorMotionSound record;
    AssertDo(mm_actor_sounds_get(&_core, i, &record), "invalid motion sound read");
    std::memcpy(out, &record, sizeof(record));
}
void ActorSoundList::Put(LegacyLong i, const ActorEvent::SoundMotionMatch *value)
{
    MMActorMotionSound record;
    std::memcpy(&record, value, sizeof(record));
    AssertDo(mm_actor_sounds_put(&_core, i, &record), "invalid motion sound write");
}
void ActorSoundList::Delete(LegacyLong i)
{
    AssertDo(mm_actor_sounds_delete(&_core, i), "invalid motion sound delete");
}
bool ActorSoundList::Valid() const
{
    return _core.count <= _core.storage.buffers[0].capacity / sizeof(MMActorMotionSound);
}
