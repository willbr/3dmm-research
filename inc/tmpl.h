/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/*************************************************************************

    tmpl.h: Actor template class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    The engine uses a C89 template owner and native archive services.

    A Template encapsulates all the data that distinguishes one actor
    "species" from another, including the species' models, actions,
    and custom texture maps.  One or more Body classes are created based
    on a Template, and the Template attaches models and materials to the body
    based on more abstract concepts like actions and costumes.

*************************************************************************/
#ifndef Template_H
#define Template_H

#include <cstdint>
#include "mm/template_create.h"
#include "mm/template_runtime.h"
#include "template_renderer.h"
#include "mm/action_definition.h"

using namespace BRender;

/****************************************
    Cel part spec: tells what model and
    xfrm to apply to a body part for
    one cel
****************************************/
struct CelPartSpec
{
    int16_t chidModl; // ChildChunkID (under Template chunk) of model for this body part
    int16_t imat34;   // index in the action's matrix table
};
static_assert(sizeof(CelPartSpec) == 4, "CelPartSpec on-disk layout drift");
const ByteOrderMask kbomCps = 0x50000000;

/****************************************
    Cel: tells what CelPartSpec's to apply to an
    actor for one cel.  It also tells
    what sound to play (if any), and how
    far the actor should move from the
    previous cel (dwr).
****************************************/
struct AnimationCel
{
    ChildChunkID chidSnd; // sound to play at this cel (ChildChunkID under ACTN)
    BRS dwr;      // distance from previous cel
                  //	CelPartSpec rgcps[];	// list of cel part specs (variable part of pggcel)
};
static_assert(sizeof(AnimationCel) == 8, "AnimationCel on-disk layout drift");
const ByteOrderMask kbomCel = 0xf0000000;

// template on file
struct TemplateOnFile
{
    int16_t bo;
    int16_t osk;
    BRA xaRest; // reminder: BRAs are shorts
    BRA yaRest;
    BRA zaRest;
    int16_t swPad; // so grftmpl (and the whole TemplateOnFile) is long-aligned
    uint32_t grftmpl;
};
static_assert(sizeof(TemplateOnFile) == 16, "TemplateOnFile on-disk layout drift");
#define kbomTmplf 0x554c0000

// action chunk on file
struct ActionChunkOnFile
{
    int16_t bo;
    int16_t osk;
    uint32_t grfactn;
};
static_assert(sizeof(ActionChunkOnFile) == 8, "ActionChunkOnFile on-disk layout drift");
const ByteOrderMask kbomActnf = 0x5c000000;

// grfactn flags
enum
{
    factnRotateX = 1, // Tells whether actor should rotate around this
    factnRotateY = 2, //   axis when following a path
    factnRotateZ = 4,
    factnStatic = 8, // Tells whether this is a stationary action
};

// grftmpl flags
enum
{
    ftmplOnlyCustomCostumes = 1, // fTrue means don't apply generic MTRLs
    ftmplTdt = 2,                // fTrue means this is a 3-D Text object
    ftmplProp = 4,               // fTrue means this is a "prop" actor
};

using PTemplate = MMTemplateOwner *;

namespace EngineTemplate
{
inline const MMTemplateOwnerHost &Host() { return *mm_kauai_template_host(); }
inline const MMActionResourceHost &ActionResourceHost() { return *mm_kauai_action_host(); }
constexpr MMResourceBlockReader ReadActionResource = mm_brender_action_read;
constexpr MMResourceBlockReader Read = mm_brender_template_read;
PTemplate Fetch(PTAG);
PTemplate Fetch(MMResourceCache *, ChunkTagOrType, ChunkNumber, bool *error = nullptr);
PTemplate Fetch(PChunkyResourceFile, ChunkTagOrType, ChunkNumber, bool *error = nullptr);
bool WriteHeader(PTemplate, PChunkyFile, ChunkTagOrType, ChunkNumber *);
void GetName(PTemplate, PString);
PBody PbodyCreate(PTemplate);
void GetRestOrien(PTemplate, BRA *, BRA *, BRA *);

inline MMTemplateState &State(PTemplate owner) { return *mm_template_owner_state(owner); }
inline bool FIsTdt(PTemplate owner) { return FPure(State(owner).flags & ftmplTdt); }
inline bool FIsProp(PTemplate owner) { return FPure(State(owner).flags & ftmplProp); }
inline LegacyLong Cactn(PTemplate owner) { return State(owner).action_count; }

bool FGetActnName(PTemplate, LegacyLong, PString);
bool FSetActnCel(PTemplate, PBody, LegacyLong action, LegacyLong cel, BRS *distance = pvNil);
bool FGetGrfactn(PTemplate, LegacyLong action, LegacyUlong *flags);
bool FGetCcelActn(PTemplate, LegacyLong action, LegacyLong *count);
bool FGetSndActnCel(PTemplate, LegacyLong action, LegacyLong cel, bool *exists, PTAG);
bool FSetDefaultCost(PTemplate, PBody);
PCustomMaterial_CMTL PcmtlFetch(PTemplate, LegacyLong id);
LegacyLong CcmidOfBset(PTemplate, LegacyLong set);
LegacyLong CmidOfBset(PTemplate, LegacyLong set, LegacyLong index);
bool FBsetIsAccessory(PTemplate, LegacyLong set);
bool FIbsetAccOfIbset(PTemplate, LegacyLong set, LegacyLong *accessory);
bool FSameAccCmids(PTemplate, LegacyLong first, LegacyLong second);

#ifdef DEBUG
void Validate(PTemplate);
void Mark(PTemplate);
#endif
}

#endif
