/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/*************************************************************************

    tmpl.h: Actor template class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BaseCacheableObject ---> ActionDefinition
    BASE ---> BaseCacheableObject ---> Template

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
    int16_t imat34;   // index into ActionDefinition's DynamicArray of transforms
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
    ChildChunkID chidSnd; // sound to play at this cel (ChildChunkID under ActionDefinition chunk)
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

/****************************************
    ActionDefinition (action): all the information
    for an action like 'rest' or 'walk'.
****************************************/
typedef class ActionDefinition *PActionDefinition;
#define ActionDefinition_PAR BaseCacheableObject
#define kclsActionDefinition 'ACTN'
class ActionDefinition : public ActionDefinition_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    MMActionDefinition _action = {};

  protected:
    ActionDefinition(void)
    {
    } // can't instantiate directly; must use FReadActn
    static MMActionDefinitionServices _Services();
    bool _FInit(PChunkyResourceFile, ChunkTagOrType, ChunkNumber);

  public:
    // Transfers both tables on success.
    static PActionDefinition PactnNew(MMActionTable *cels, MMActionTable *matrices, LegacyUlong grfactn);
    static int FReadActn(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *,
                           void **object, MMI32 *bytes);
    ~ActionDefinition(void);

    LegacyUlong Grfactn(void)
    {
        return _action.flags;
    }

    LegacyLong Ccel(void);
    bool FGetCel(LegacyLong icel, AnimationCel *pcel);
    bool FGetCps(LegacyLong icel, LegacyLong icps, CelPartSpec *pcps);
    bool FGetMatrix(LegacyLong imat34, BMAT34 *pbmat34);
    void GetSnd(LegacyLong icel, PTAG ptagSnd);
};

// grftmpl flags
enum
{
    ftmplOnlyCustomCostumes = 1, // fTrue means don't apply generic MTRLs
    ftmplTdt = 2,                // fTrue means this is a 3-D Text object
    ftmplProp = 4,               // fTrue means this is a "prop" actor
};

/****************************************
    Template: The template class.
    anid is an action ID.
    cmid is a costume ID.
    celn is a cel number.
****************************************/
typedef class Template *PTemplate;
#define Template_PAR BaseCacheableObject
#define kclsTemplate 'TMPL'
class Template : public Template_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    MMTemplateState _template; // Owns parent/set/costume tables and metadata.
    String _stn;         // Template name

  protected:
    Template(void)
    {
        mm_template_state_init(&_template);
    } // can't instantiate directly; must use FReadTmpl
    static MMTemplateCreateServices _CreateServices(PChunkyResourceFile cache = nullptr);
    static MMTemplateWriter _Writer(PChunkyFile);
    MMTemplateRuntimeServices _RuntimeServices();
    bool _FReadTmplf(PChunkyResourceFile, ChunkTagOrType, ChunkNumber, MMOwnedBlock *);
    virtual bool _FInit(PChunkyResourceFile, ChunkTagOrType, ChunkNumber, MMOwnedBlock *);
    virtual PActionDefinition _PactnFetch(LegacyLong anid);
    virtual PModel _PmodlFetch(ChildChunkID chidModl);
    bool _FWriteTmplf(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber *pcno);

  public:
    static int FReadTmpl(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *,
                           void **object, MMI32 *bytes);
    ~Template(void);

    // Template / Body stuff
    void GetName(PString pstn); // default name of actor or text of the ThreeDText
    PBody PbodyCreate(void); // Creates a body based on this Template
    void GetRestOrien(BRA *pxa, BRA *pya, BRA *pza);
    bool FIsTdt(void)
    {
        return FPure(_template.flags & ftmplTdt);
    }
    bool FIsProp(void)
    {
        return FPure(_template.flags & ftmplProp);
    }

    // Action stuff
    LegacyLong Cactn(void)
    {
        return _template.action_count;
    } // count of actions
    virtual bool FGetActnName(LegacyLong anid, PString pstn);
    bool FSetActnCel(PBody pbody, LegacyLong anid, LegacyLong celn, BRS *pdwr = pvNil);
    bool FGetGrfactn(LegacyLong anid, LegacyUlong *pgrfactn);
    bool FGetDwrActnCel(LegacyLong anid, LegacyLong celn, BRS *pdwr);
    bool FGetCcelActn(LegacyLong anid, LegacyLong *pccel);
    bool FGetSndActnCel(LegacyLong anid, LegacyLong celn, bool *pfSoundExists, PTAG ptag);

    // Costume stuff
    virtual bool FSetDefaultCost(PBody pbody); // applies default costume
    virtual PCustomMaterial_CMTL PcmtlFetch(LegacyLong cmid);
    LegacyLong CcmidOfBset(LegacyLong ibset);
    LegacyLong CmidOfBset(LegacyLong ibset, LegacyLong icmid);
    bool FBsetIsAccessory(LegacyLong ibset); // whether ibset holds accessories
    bool FIbsetAccOfIbset(LegacyLong ibset, LegacyLong *pibsetAcc);
    bool FSameAccCmids(LegacyLong cmid1, LegacyLong cmid2);
};

#endif Template_H
