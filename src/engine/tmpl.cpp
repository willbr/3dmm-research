/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tmpl.cpp: Actor template class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    Here's the Template chunk tree (* means more than one chunk can go here):

    Template - template flags
     |
     +--GLPI (chid 0) - parent IDs (Body part hierarchy)
     |
     +--GLBS (chid 0) - body part sets for Body
     |
     +--GGCM (chid 0) - custom costumes per body part set (GeneralGroup of cmids)
     |
     +--CustomMaterial_CMTL* (chid <cmid>) - custom material...see mtrl.h
     |   |
     |   +--Material_MTRL*
     |       |
     |       +--TextureMap
     |
     +--Model* - models used in this template
     |
     +--ACTN (chid <anid>) - action for this template
         |
         +--GGCL (chid 0) - GeneralGroup of cels for this action
         |
         +--GLXF (chid 0) - DynamicArray of transformation matrices for this action
         |
         +--GLMS (chid 0) - DynamicArray of motionmatch sounds for cels of this action

    About Actions: An action is an activity that a body can perform, such
    as walking, jumping, breathing, or resting.  Actions are broken down
    into cels, where each cel describes the position of each body part of
    the actor at one step or phase of the action.  After reaching the last
    cel, the action loops around to the beginning cel and repeats.  Cels
    consist of a list of cel part specs (CelPartSpec).  Each CelPartSpec refers to a single
    body part of an actor, such as a leg or head.  The CelPartSpec tells what
    BRender model to use for the body part for this cel and what matrix to
    use to orient it to its parent body part.  Each time the cel number is
    changed, Template reads each CelPartSpec of the new cel and updates each body part
    to use the new model and transformation matrix.

    About the GGCM: it is indexed by body part set, and tells how many
    custom materials are available for each body part set, and what the
    CMIDs are for those materials.  The first CMID in each body part set
    is the default one (for the default costume).  Example: If a body had
    3 body part sets (shirt, pants, and head), and there are 2 shirts,
    3 pants, and 1 head, the GGCM would be:

    0: fixed = 2, variable = (0, 1)
    1: fixed = 3, variable = (2, 3, 4)
    2: fixed = 1, variable = (5)

    where the fixed part is the count of available CMIDs for each body
    part set, and the numbers in the variable part are the CMIDs for the
    respective body part sets.  The default costume for this actor would
    be CMID 0 for the shirt, 2 for the pants, and 5 for the head.

***************************************************************************/
#include "soc_core.h"
ASSERTNAME

#ifdef DEBUG
#define TEMPLATE_VALIDATE(owner) EngineTemplate::Validate(owner)
#else
#define TEMPLATE_VALIDATE(owner) ((void)0)
#endif

PTemplate EngineTemplate::Fetch(MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number, bool *error)
{
    int failed = 0;
    auto owner = mm_brender_template_fetch(cache, type, number, &failed);
    if (error)
        *error = failed != 0;
    return owner;
}

bool EngineTemplate::WriteHeader(PTemplate owner, PChunkyFile file, ChunkTagOrType type, ChunkNumber *number)
{
    TEMPLATE_VALIDATE(owner); AssertPo(file, 0); AssertVarMem(number);
    const auto writer = file->WriteIndex();
    byte wire[kcbMaxDataStn];
    String name;
    GetName(owner, &name);
    const auto encoded = ChunkyFile::EncodeName(&name, wire);
    return mm_template_write_header(&State(owner), &encoded, type, number, &writer) != 0;
}

/***************************************************************************
    Creates a new tree of body parts (br_actors) based on this template.
    Note: Actor also calls FSetDefaultCost after creating the body, but
    by calling it here, it is guaranteed that the body will have a material
    on each body part (no null pointers for bact->material).  So the user
    will never see a body part that isn't texture mapped.
***************************************************************************/
PBody EngineTemplate::PbodyCreate(PTemplate owner)
{
    return mm_brender_template_body(owner);
}

bool EngineTemplate::FGetActnName(PTemplate owner, LegacyLong anid, PString pstn)
{
    AssertIn(anid, 0, State(owner).action_count); AssertPo(pstn, 0);
    const MMStringValue name = {pstn->Pst(), sizeof(achar)};
    return mm_brender_template_action_name(owner, anid, &name) != 0;
}

/***************************************************************************
    Sets up the body part tree to use the correct models and transformation
    matrices for the given cel of the given action.  Also returns the
    distance to the next cel in *pdwr.
***************************************************************************/
bool EngineTemplate::FSetActnCel(PTemplate owner, PBody pbody, LegacyLong anid, LegacyLong celn, BRS *pdwr)
{
    Assert(pbody, "missing body"); AssertIn(anid, 0, State(owner).action_count); AssertNilOrVarMem(pdwr);
    return mm_brender_template_set_cel(owner, pbody, anid, celn, pdwr) != 0;
}

/***************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool EngineTemplate::FGetDwrActnCel(PTemplate owner, LegacyLong anid, LegacyLong celn, BRS *pdwr)
{
    AssertIn(anid, 0, State(owner).action_count); AssertVarMem(pdwr);
    return mm_brender_template_distance(owner, anid, celn, pdwr) != 0;
}

/***************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool EngineTemplate::FGetCcelActn(PTemplate owner, LegacyLong anid, LegacyLong *pccel)
{
    AssertIn(anid, 0, State(owner).action_count); AssertVarMem(pccel);
    return mm_brender_template_cel_count(owner, anid, pccel) != 0;
}

/***************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool EngineTemplate::FGetSndActnCel(PTemplate owner, LegacyLong anid, LegacyLong celn, bool *pfSoundExists, PTAG ptag)
{
    AssertIn(anid, 0, State(owner).action_count); AssertVarMem(pfSoundExists); AssertVarMem(ptag);
    static_assert(sizeof(TAG) == sizeof(MMTag) && offsetof(TAG, sid) == offsetof(MMTag, sid) &&
                      offsetof(TAG, cache) == offsetof(MMTag, cache) && offsetof(TAG, ctg) == offsetof(MMTag, type) &&
                      offsetof(TAG, cno) == offsetof(MMTag, number), "sound tag layout");
    MMTag tag;
    std::memcpy(&tag, ptag, sizeof(tag));
    int exists;
    const int ok = mm_brender_template_sound(owner, anid, celn, &exists, &tag);
    std::memcpy(ptag, &tag, sizeof(tag));
    *pfSoundExists = exists != 0;
    return ok != 0;
}

/***************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool EngineTemplate::FGetGrfactn(PTemplate owner, LegacyLong anid, LegacyUlong *pgrfactn)
{
    AssertIn(anid, 0, State(owner).action_count); AssertVarMem(pgrfactn);
    return mm_brender_template_action_flags(owner, anid, pgrfactn) != 0;
}

/***************************************************************************
    Get orientation for template when actor has no path
***************************************************************************/
void EngineTemplate::GetRestOrien(PTemplate owner, BRA *pxa, BRA *pya, BRA *pza)
{
    AssertVarMem(pxa);
    AssertVarMem(pya);
    AssertVarMem(pza);

    mm_brender_template_rest(owner, pxa, pya, pza);
}

/***************************************************************************
    Puts default costume on pbody
***************************************************************************/
bool EngineTemplate::FSetDefaultCost(PTemplate owner, PBody pbody)
{
    Assert(pbody, "missing body");
    return mm_brender_template_default_costume(owner, pbody) != 0;
}

/***************************************************************************
    Returns the number of custom materials available for ibset
***************************************************************************/
LegacyLong EngineTemplate::CcmidOfBset(PTemplate owner, LegacyLong ibset)
{
    AssertIn(ibset, 0, State(owner).set_count);
    return mm_brender_template_costume_count(owner, ibset);
}

/***************************************************************************
    Returns the icmid'th CMID available for ibset
***************************************************************************/
LegacyLong EngineTemplate::CmidOfBset(PTemplate owner, LegacyLong ibset, LegacyLong icmid)
{
    AssertIn(ibset, 0, State(owner).set_count); AssertIn(icmid, 0, CcmidOfBset(owner, ibset));
    return mm_brender_template_costume_id(owner, ibset, icmid);
}

/***************************************************************************
    Tells whether ibset holds accessories by checking to see if one of
    its costumes has model children.
***************************************************************************/
bool EngineTemplate::FBsetIsAccessory(PTemplate owner, LegacyLong ibset)
{
    AssertIn(ibset, 0, State(owner).set_count);
    return mm_brender_template_is_accessory(owner, ibset) != 0;
}

/***************************************************************************
    Returns the ibset of the accessory associated with ibset, if any.  If
    ibset is itself an accessory, it is returned in *pibsetAcc.  Otherwise,
    if ibset is the parent of an accessory, that accessory is returned in
    *pibsetAcc.
***************************************************************************/
bool EngineTemplate::FIbsetAccOfIbset(PTemplate owner, LegacyLong ibset, LegacyLong *pibsetAcc)
{
    AssertIn(ibset, 0, State(owner).set_count); AssertVarMem(pibsetAcc);
    return mm_brender_template_accessory_set(owner, ibset, pibsetAcc) != 0;
}

/***************************************************************************
    See if cmid1 and cmid2 are for the same accessory by comparing child
    model chunks
***************************************************************************/
bool EngineTemplate::FSameAccCmids(PTemplate owner, LegacyLong cmid1, LegacyLong cmid2)
{
    return mm_brender_template_same_accessory(owner, cmid1, cmid2) != 0;
}

/***************************************************************************
    Get a custom material.  The cmid is really the chid under the Template.
***************************************************************************/
PCustomMaterial_CMTL EngineTemplate::PcmtlFetch(PTemplate owner, LegacyLong cmid)
{
    TEMPLATE_VALIDATE(owner);
    if (mm_template_owner_is_text(owner))
    {
        AssertIn(cmid, 0, State(owner).custom_count);
        Bug("Shouldn't fetch CMTLs from a ThreeDText");
        return pvNil;
    }
    return mm_brender_template_costume(owner, cmid);
}

/***************************************************************************
    Puts the template's name into pstn
***************************************************************************/
void EngineTemplate::GetName(PTemplate owner, PString pstn)
{
    TEMPLATE_VALIDATE(owner);
    AssertPo(pstn, 0);

    pstn->SetSt(static_cast<PPascalString>(mm_template_owner_name(owner).buffer));
}

#ifdef DEBUG
void EngineTemplate::Validate(PTemplate owner)
{
    mm_kauai_template_validate(owner);
}

void EngineTemplate::Mark(PTemplate owner)
{
    mm_kauai_template_mark(owner);
}
#endif // DEBUG
