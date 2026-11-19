/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef ACTOR_TRACK_H
#define ACTOR_TRACK_H
#include "actor_data.h"
#include "mm/actor_events.h"
#include "mm/actor_path.h"
#include "mm/actor_track.h"

// Owns authored route/event tables and the embedded resource references in
// their TAGs. Tables read by ActorData start with unopened TAGs; inserting an
// opened TAG transfers a reference to the track. Playback state stays outside.
// Mutable tables must use validated ActorData runtime layouts. Before deleting
// or replacing a TAG-bearing row, close its opened TAG; before inserting a
// borrowed opened TAG, duplicate its reference. Replacing a whole table also
// requires releasing its previous contents (Clear handles both tables).
class ActorTrack
{
    MMActorTrack _storage = {};

  public:
    MMActorEventTable *Events() const
    {
        return static_cast<MMActorEventTable *>(_storage.events);
    }
    MMActorPath *Route() const
    {
        return static_cast<MMActorPath *>(_storage.route);
    }
    MMActorEventTable *AdoptEvents(MMActorEventTable *);
    MMActorPath *AdoptRoute(MMActorPath *);

    ActorTrack() = default;
    ~ActorTrack();
    ActorTrack(const ActorTrack &) = delete;
    ActorTrack &operator=(const ActorTrack &) = delete;
    bool FCreate();
    // Transactional deep copy, including references owned by opened TAGs.
    bool FCloneFrom(const ActorTrack &source);
    void Swap(ActorTrack &other);
    void Clear();
    void CloseTags();
    // Legacy byte-address accessor: group rows have four-byte alignment.
    // Copy the returned TAG to an aligned local before accessing its fields.
    static bool FEventTag(MMActorEventTable *events, LegacyLong index, PTAG *tag, ActorEvent::PBase *event = pvNil);
};
#endif
