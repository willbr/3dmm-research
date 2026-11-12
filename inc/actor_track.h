/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef ACTOR_TRACK_H
#define ACTOR_TRACK_H
#include "actor_data.h"
#include "three_mm/actor_track.h"

// Owns authored route/event tables and the embedded resource references in
// their TAGs. Tables read by ActorData start with unopened TAGs; inserting an
// opened TAG transfers a reference to the track. Playback state stays outside.
// Mutable tables must use validated ActorData runtime layouts. Before deleting
// or replacing a TAG-bearing row, close its opened TAG; before inserting a
// borrowed opened TAG, duplicate its reference. Replacing a whole table also
// requires releasing its previous contents (Clear handles both tables).
class ActorTrack
{
    ThreeMMActorTrack _storage = {};

  public:
    PGeneralGroup Events() const
    {
        return static_cast<PGeneralGroup>(_storage.events);
    }
    PDynamicArray Route() const
    {
        return static_cast<PDynamicArray>(_storage.route);
    }
    PGeneralGroup AdoptEvents(PGeneralGroup);
    PDynamicArray AdoptRoute(PDynamicArray);

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
    static bool FEventTag(PGeneralGroup events, LegacyLong index, PTAG *tag, ActorEvent::PBase *event = pvNil);
};
#endif
