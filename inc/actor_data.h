/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef ACTOR_DATA_H
#define ACTOR_DATA_H
#include "kauai_core.h"
#include "bren_core.h"
#include "tagman.h"
#include <cstdint>
using namespace BRender;

struct RoutePoint
{
    BRS dxr;
    BRS dyr;
    BRS dzr;

    bool operator==(RoutePoint &xyz)
    {
        return ((dxr == xyz.dxr) && (dyr == xyz.dyr) && (dzr == xyz.dzr));
    }
    bool operator!=(RoutePoint &xyz)
    {
        return ((dxr != xyz.dxr) || (dyr != xyz.dyr) || (dzr != xyz.dzr));
    }
};
static_assert(sizeof(RoutePoint) == 12, "RoutePoint on-disk layout drift");

typedef RoutePoint *PRoutePoint;

const ByteOrderMask kbomXyz = 0xfc000000;

//
// 	A RouTE is a general list (DynamicArray) of Route PoinTs(RouteDistancePoint)
//  A Subroute is a contiguous section of a route.
//
struct RouteDistancePoint
{
    RoutePoint xyz;
    BRS dwr; // Distance from this node to the next node on the route
};
static_assert(sizeof(RouteDistancePoint) == 16, "RouteDistancePoint on-disk layout drift");
const ByteOrderMask kbomRpt = 0xff000000;

const LegacyLong knfrmInvalid = klwMax;
const BRS kdwrNil = BR_SCALAR(-1.0); // Authored step sentinel: use the action/cel step.
struct RouteLocation // RouTE Location - a function of space and time
{
    int32_t irpt;  // The preceding node for the given point
    BRS dwrOffset; // Absolute linear distance beyond node irpt
    int32_t dnfrm; // Delta frame number (ie, time) at this point

    bool operator==(RouteLocation &rtel)
    {
        return (irpt == rtel.irpt && dwrOffset == rtel.dwrOffset && dnfrm == rtel.dnfrm);
    }

    bool operator!=(RouteLocation &rtel)
    {
        return (irpt != rtel.irpt || dwrOffset != rtel.dwrOffset || dnfrm != rtel.dnfrm);
    }

    bool operator<=(RouteLocation &rtel)
    {
        return (irpt < rtel.irpt || (irpt == rtel.irpt && (dwrOffset < rtel.dwrOffset ||
                                                           (dwrOffset == rtel.dwrOffset && dnfrm <= rtel.dnfrm))));
    }

    bool operator>=(RouteLocation &rtel)
    {
        return (irpt > rtel.irpt || (irpt == rtel.irpt && (dwrOffset > rtel.dwrOffset ||
                                                           (dwrOffset == rtel.dwrOffset && dnfrm >= rtel.dnfrm))));
    }

    bool operator<(RouteLocation &rtel)
    {
        return (irpt < rtel.irpt || (irpt == rtel.irpt && (dwrOffset < rtel.dwrOffset ||
                                                           (dwrOffset == rtel.dwrOffset && dnfrm < rtel.dnfrm))));
    }

    bool operator>(RouteLocation &rtel)
    {
        return (irpt > rtel.irpt || (irpt == rtel.irpt && (dwrOffset > rtel.dwrOffset ||
                                                           (dwrOffset == rtel.dwrOffset && dnfrm > rtel.dnfrm))));
    }
};
static_assert(sizeof(RouteLocation) == 12, "RouteLocation on-disk layout drift");

namespace ActorEvent
{

// Actor EVents are stored in a GeneralGroup (general group)
// Fixed part of the GeneralGroup:
struct Base
{
    int32_t aet;        // Actor Event Type
    int32_t nfrm;       // Absolute frame number (* Only valid < current event)
    RouteLocation rtel; // RouTE Location for this event
};                      // Additional event parameters (in the GeneralGroup)
static_assert(sizeof(Base) == 20, "ActorEvent::Base on-disk layout drift");
typedef Base *PBase;

//
//	Actor level Event Types which live in a GeneralGroup.
//	The fixed part of a GeneralGroup entry is an actor event (aev)
//	The variable part is documented in the following comments
//
enum ActorEventType
{
    aetAdd,    // Add Actor onstage: aevadd - internal (no api)
    aetActn,   // Animate Actor : aevactn
    aetCost,   // Set Costume : aevcost
    aetRotF,   // Transform Actor rotate: BMAT34
    aetPull,   // Transform Actor Pull : aevpull
    aetSize,   // Transform Actor size uniformly : BRS
    aetSnd,    // Play a sound : aevsnd
    aetMove,   // Translate the path at this point : RoutePoint
    aetFreeze, // Freeze (or Un) Actor : long
    aetTweak,  // Path tweak : RoutePoint
    aetStep,   // Force step size (eg, float, wait) : BRS
    aetRem,    // Remove an actor from the stage : nil
    aetRotH,   // Single frame rotation : BMAT34
    aetLim
};

const ByteOrderMask kbomAet = 0xc0000000;
const ByteOrderMask kbomAev = 0xffc00000;

//
//	Variable part of the Actor EVent GeneralGroup:
//
struct Stretch // Squash/stretch
{
    BRS rScaleX;
    BRS rScaleY;
    BRS rScaleZ;
};
static_assert(sizeof(Stretch) == 12, "ActorEvent::Stretch on-disk layout drift");
const ByteOrderMask kbomAevpull = 0xfc000000;

// Every subroute is normalized.  The normalization translation is
// stored in the Add Event
// ** nfrmPrev valid for nfrmSub <= _nfrmCur only (optimization)
struct Add
{
    BRS dxr; // Translation in x for this subroute
    BRS dyr; // Translation in y for this subroute
    BRS dzr; // Translation in z for this subroute
    BRA xa;  // Single point orientation
    BRA ya;  // Single point orientation
    BRA za;  // Single point orientation
};
static_assert(sizeof(Add) == 20, "ActorEvent::Add on-disk layout drift");
const ByteOrderMask kbomAevadd = 0xfd500000;

struct Action
{
    int32_t anid;
    int32_t celn; // starting cel of action
};
static_assert(sizeof(Action) == 8, "ActorEvent::Action on-disk layout drift");
const ByteOrderMask kbomAevactn = 0xf0000000;

// Runtime form: embeds full TAG (with runtime pcrf). Allowed to grow on x64.
// Marshalled to/from CostumeOnFile at the GG I/O boundary in actrsave.cpp.
struct Costume
{
    int32_t ibset; // body part set
    int32_t cmid;  // costume ID (for custom costumes)
    tribool fCmtl; // vs fMtrl
    TAG tag;
};
const ByteOrderMask kbomAevcost = 0xfc000000 | (kbomTag >> 6);

// Wire format for Costume. Fixed at 28 bytes on every architecture.
struct CostumeOnFile
{
    int32_t ibset;
    int32_t cmid;
    tribool fCmtl;
    TAGOnFile tag;
};
static_assert(sizeof(CostumeOnFile) == 28, "ActorEvent::CostumeOnFile wire format drift");

// Runtime form: embeds full TAG. Allowed to grow on x64.
struct Sound
{
    tribool fLoop;     // loop count
    tribool fQueue;    // queued sound
    int32_t vlm;       // volume
    int32_t celn;      // motion match	: ivNil if not
    int32_t sty;       // sound type
    tribool fNoSound;  // no sound
    ChildChunkID chid; // user sound requires chid
    TAG tag;
};
const ByteOrderMask kbomAevsnd = 0xfffffc00;

// Wire format for Sound. Fixed at 44 bytes on every architecture.
struct SoundOnFile
{
    tribool fLoop;
    tribool fQueue;
    int32_t vlm;
    int32_t celn;
    int32_t sty;
    tribool fNoSound;
    ChildChunkID chid;
    TAGOnFile tag;
};
static_assert(sizeof(SoundOnFile) == 44, "ActorEvent::SoundOnFile wire format drift");

const ByteOrderMask kbomAevsize = 0xc0000000;
const ByteOrderMask kbomAevfreeze = 0xc0000000;
const ByteOrderMask kbomAevstep = 0xc0000000;
const ByteOrderMask kbomAevmove = kbomXyz;
const ByteOrderMask kbomAevtweak = kbomXyz;
const ByteOrderMask kbomAevrot = kbomBmat34;

// Separate ggaev variable portion sizes
#define kcbVarAdd (size(Add))
#define kcbVarActn (size(Action))
#define kcbVarCost (size(Costume))
#define kcbVarRot (size(BMAT34))
#define kcbVarSize (size(BRS))
#define kcbVarPull (size(Stretch))
#define kcbVarSnd (size(Sound))
#define kcbVarFreeze (size(int32_t))
#define kcbVarMove (size(RoutePoint))
#define kcbVarTweak (size(RoutePoint))
#define kcbVarStep (size(BRS))
#define kcbVarZero (0)

// Actor Event Flags
enum
{
    faetNil = 0,
    faetAdd = 1 << aetAdd,
    faetActn = 1 << aetActn,
    faetCost = 1 << aetCost,
    faetRotF = 1 << aetRotF,
    faetPull = 1 << aetPull,
    faetSize = 1 << aetSize,
    faetFreeze = 1 << aetFreeze,
    faetTweak = 1 << aetTweak,
    faetStep = 1 << aetStep,
    faetRem = 1 << aetRem,
    faetMove = 1 << aetMove,
    faetRotH = 1 << aetRotH
};

//
// Because rotations are non-abelian, cumulative rotations are stored
// in a BMAT34.  It would not be correct to multiply (xa,ya,za) values
// treating x,y,z independently.
//
// Bmat34Fwd does NOT include the path portion of the orientation.
// Bmat34Cur includes all current angular rotation - path rotation is NOT post
// applied to this.
//
// Careful: The task is complicated by the fact that users can apply single
// frame and frame forward orientations in the same frame and must not see the
// actor jump in angle when choosing between the two methods of editing.
//
struct FramePosition
{
    BMAT34 bmat34Fwd; // Rotation	fwd	: path rotation post applied to this
    Stretch aevpull;  // Stretching (pulling) constants
    BRS rScaleStep;   // Uniform scaling to be applied to step size
    BMAT34 bmat34Cur; // <<Path independent>> Single frame & static segment rotation
    BRA xaPath;       // Path portion of the current frame's rotation
    BRA yaPath;       // Path portion of the current frame's rotation
    BRA zaPath;       // Path portion of the current frame's rotation
};

//
// Current action Motion Match Sounds
// Aev & Aevsnd grouped together form a gl
// Not to be saved with a movie
//
struct SoundMotionMatch
{
    Base aev; // event for the sound
    Sound aevsnd;
};

} // end of namespace ActorEvent
struct ActorChunkOnFile // Actor chunk on file
{
    int16_t bo;             // Byte order
    int16_t osk;            // OS kind
    RoutePoint dxyzFullRte; // Translation of the route
    int32_t arid;           // Unique id assigned to this actor.
    int32_t nfrmFirst;      // First frame in this actor's stage life
    int32_t nfrmLast;       // Last frame in this actor's stage life
    TAGOnFile tagTmpl;      // Tag to actor's template (16-byte wire format; convert with From / TagFromOnFile)
};
static_assert(sizeof(ActorChunkOnFile) == 44, "ActorChunkOnFile on-disk layout drift");
const ByteOrderMask kbomActf = 0x5fffff00;

// Actor persistence without Scene, Movie, World, audio or UI objects.
struct ActorData
{
    static bool FReadHeader(PDataBlock block, ActorChunkOnFile *header);
    static bool FWriteHeader(PChunkyFile file, ChunkNumber number, ActorChunkOnFile *header);
    static bool FAdjustId(PChunkyFile file, ChunkNumber number, LegacyLong delta);
    static PDynamicArray PglReadRoute(PDataBlock block);
    // Returned events have runtime-width values with unopened TAGs (pcrf=nil).
    static PGeneralGroup PggReadEvents(PDataBlock block);
    // Borrows runtime events. Returned wire values own no TAG references.
    static PGeneralGroup PggWriteEvents(PGeneralGroup events);
};
#endif
