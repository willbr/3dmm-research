/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    ape.h: Actor preview entity

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> CommandHandler ---> GraphicsObject ---> ActorPreviewEntity

***************************************************************************/
#ifndef APE_H
#define APE_H

// ActorPreviewEntity tool types
enum
{
    aptNil = 0,
    aptIncCmtl,      // Increment CMTL
    aptIncAccessory, // Increment Accessory
    aptGms,          // Material (Material_MTRL or CMTL)
    aptLim
};

// Generic material spec
struct GeneralMaterialSpec
{
    bool fValid; // if fFalse, ignore this GeneralMaterialSpec
    bool fMtrl;  // if fMtrl is fTrue, tagMtrl is valid.  Else cmid is valid
    long cmid;
    TAG tagMtrl;
};

// Actor preview entity tool
struct ActorPreviewEntityTool
{
    long apt;
    GeneralMaterialSpec gms;
};

/****************************************
    Actor preview entity class
****************************************/
typedef class ActorPreviewEntity *PActorPreviewEntity;
#define ActorPreviewEntity_PAR GraphicsObject
#define kclsActorPreviewEntity 'APE'
class ActorPreviewEntity : public ActorPreviewEntity_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ActorPreviewEntity)

  protected:
    PWorld _pbwld;       // BRender world to draw actor in
    PTMPL _ptmpl;       // Template (or TDT) of the actor being previewed
    PBODY _pbody;       // Body of the actor being previewed
    ActorPreviewEntityTool _apet;         // Currently selected tool
    PDynamicArray _pglgms;        // What materials are attached to what body part sets
    long _celn;         // Current cel of action
    Clock _clok;         // To time cel cycling
    BLIT _blit;         // BRender light data
    BACT _bact;         // BRender light actor
    long _anid;         // Current action ID
    long _iview;        // Current camera view
    bool _fCycleCels;   // If cycling cels
    PRCA _prca;         // resource source (for cursors)
    long _ibsetOnlyAcc; // ibset of accessory, if only one (else ivNil)

  protected:
    ActorPreviewEntity(PGCB pgcb) : GraphicsObject(pgcb), _clok(CommandHandler::HidUnique())
    {
    }
    bool _FInit(PTMPL ptmpl, PCOST pcost, long anid, bool fCycleCels, PRCA prca);
    void _InitView(void);
    void _SetScale(void);
    void _UpdateView(void);
    bool _FApplyGms(GeneralMaterialSpec *pgms, long ibset);
    bool _FIncCmtl(GeneralMaterialSpec *pgms, long ibset, bool fNextAccessory);
    long _CmidNext(long ibset, long icmidCur, bool fNextAccessory);

  public:
    static PActorPreviewEntity PapeNew(PGCB pgcb, PTMPL ptmpl, PCOST pcost, long anid, bool fCycleCels, PRCA prca = pvNil);
    ~ActorPreviewEntity();

    void SetToolMtrl(PTAG ptagMtrl);
    void SetToolCmtl(long cmid);
    void SetToolIncCmtl(void);
    void SetToolIncAccessory(void);

    bool FSetAction(long anid);
    bool FCmdNextCel(PCommand pcmd);

    void SetCustomView(BRA xa, BRA ya, BRA za);
    void ChangeView(void);
    virtual void Draw(PGraphicsEnvironment pgnv, RC *prcClip);
    virtual bool FCmdMouseMove(PCMD_MOUSE pcmd);
    virtual bool FCmdTrackMouse(PCMD_MOUSE pcmd);

    bool FChangeTdt(PString pstn, long tdts, PTAG ptagTdf);
    bool FSetTdtMtrl(PTAG ptagMtrl);
    bool FGetTdtMtrlCno(ChunkNumber *pcno);

    void GetTdtInfo(PString pstn, long *ptdts, PTAG ptagTdf);
    long Anid(void)
    {
        return _anid;
    }
    long Celn(void)
    {
        return _celn;
    }
    void SetCycleCels(bool fOn);
    bool FIsCycleCels(void)
    {
        return _fCycleCels;
    }
    bool FDisplayCel(long celn);
    long Cbset(void)
    {
        return _pbody->Cbset();
    }

    // Returns fTrue if a material was applied to this ibset
    bool FGetMaterial(long ibset, tribool *pfMtrl, long *pcmid, TAG *ptagMtrl);
};

#endif APE_H
