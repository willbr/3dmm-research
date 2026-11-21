/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/*************************************************************************

    body.h: Body class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> Body

*************************************************************************/
#ifndef Body_H
#define Body_H

#include "mm/body_create.h"
#include "mm/part_indices.h"
#include "mm/body_edit.h"
#include "mm/body_runtime.h"
#include "mm/render_frame.h"

using namespace BRender;

/****************************************
    The Body class
****************************************/
typedef class Body *PBody;
#define Body_PAR BASE
#define kclsBody 'BODY'
class Body : public Body_PAR
{
    friend class BodyWorld;
    friend class BodyCostume;
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    MMBodyState _body;

  protected:
    Body(void)
    {
        mm_body_state_init(&_body);
    } // can't instantiate directly; must use PbodyNew
    static MMBodyCreateServices _CreateServices(void);
    static MMBodyEditServices _EditServices(void);
    static MMBodyRuntimeServices _RuntimeServices(void);
    static MMBodyGeometryServices _GeometryServices(void);
    void _DestroyShape(void);
    PBACT _Actors(void) const { return static_cast<PBACT>(_body.shape.actors); }
    PBACT _PbactRoot(void) // ptr to root BACT
    {
        return _Actors();
    }
    PBACT _PbactHilite(void) // ptr to hilite BACT
    {
        return _Actors() + 1;
    }                            // skip root BACT
    PBACT _PbactPart(LegacyLong ipart) // ptr to ipart'th body part
    {
        return _Actors() + 1 + 1 + ipart;
    }                 // skip root and hilite BACTs
    LegacyLong _Cbact(void) // count in _Actors()
    {
        return 1 + 1 + _body.shape.part_count;
    }                       // root, hilite, and body part BACTs
    LegacyLong _Ibset(LegacyLong ipart) // body part set that this part belongs to
    {
        if (ipart < 0 || ipart >= _body.shape.part_count)
            return ivNil;
        return _body.shape.bindings[ipart+2].set;
    }

    // Callbacks from BRender:
    static void _BactRendered(PBACT pbact, RC *prc);
    static int _ReadRenderBody(PBACT, MMRenderBody *);
    static void _PrepareRenderBody(const MMRenderBody *);
    static void _GetRc(PBACT pbact, RC *prc);

  public:
    static PBody PbodyNew(PDynamicArray pglibactPar, PDynamicArray pglibset);
    static PBody PbodyNew(const MMPartIndexView &parents, const MMPartIndexView &sets);
    static PBody PbodyFromBact(BACT *pbact, LegacyLong *pibset = pvNil);
    /* Public accessors for the BACT tree -- needed by headless test
     * harnesses that walk the body to render via BrZbScene without
     * going through the full World/PWorld playback runtime. */
    PBACT PbactRoot(void) { return _PbactRoot(); }
    PBACT PbactPart(LegacyLong ipart) { return _PbactPart(ipart); }
    LegacyLong Cbact(void) { return _Cbact(); }
    ~Body(void);
    PBody PbodyDup(void);
    void Restore(PBody pbodyDup);

    bool FChangeShape(PDynamicArray pglibactPar, PDynamicArray pglibset);
    bool FChangeShape(const MMPartIndexView &parents, const MMPartIndexView &sets);
    // Binding lifetime must cover the body and any duplicates. Configure
    // while hidden; the core owns its actor tree, the adapter owns its host.
    void SetRenderCallbacks(void *context, void (*attach)(void *, void *), void (*changed)(void *));

    void Show(void);
    void Hide(void);
    void Hilite(void);
    void Unhilite(void);

    static void SetHiliteColor(LegacyLong iclr);
    // Release after all bodies, before shutting down BRender.
    static void ReleaseSharedMaterial(void);

    void LocateOrient(BRS xr, BRS yr, BRS zr, BMAT34 *pbmat34);
    void ApplyPose(const MMActorBodyPose &);
    const MMActorMatrix &RootMatrix() const { return _body.shape.bindings[0].matrix; }
    MMBodyDrawView DrawView() const { return {_body.shape.bindings, _body.shape.part_count}; }
    void PartWorldMatrix(LegacyLong part, MMActorMatrix &) const;
    void SetPartModel(LegacyLong ibact, Model *pmodl);
    void SetPartMatrix(LegacyLong ibact, BMAT34 *pbmat34);
    void SetPartMatrix(LegacyLong ibact, const MMActorMatrix &);
    void SetPartSetMtrl(LegacyLong ibset, Material_MTRL *pmtrl);
    void SetPartSetCmtl(CustomMaterial_CMTL *pcmtl);
    void GetPartSetMaterial(LegacyLong ibset, bool *pfMtrl, Material_MTRL **ppmtrl, CustomMaterial_CMTL **ppcmtl);
    LegacyLong Cbset()
    {
        return _body.shape.set_count;
    }

    void GetBcbBounds(BCB *pbcb, bool fWorld = fFalse);
    void GetRcBounds(RC *prc);
    void GetCenter(LegacyLong *pxp, LegacyLong *pyp);
    void GetPosition(BRS *pxr, BRS *pyr, BRS *pzr);
    bool FIsInView(void);
};

/****************************************
    The BodyCostume class, which is used to
    save and restore a Body's entire
    costume for unwinding purposes
****************************************/
typedef class BodyCostume *PBodyCostume;
#define BodyCostume_PAR BASE
#define kclsBodyCostume 'COST'
class BodyCostume : public BodyCostume_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    MMBodyCostume _costume; // C-owned material references

  private:
    void _Clear(void); // release C-owned entries and material references

  public:
    BodyCostume(void);
    ~BodyCostume(void);

    bool FGet(PBody pbody); // read and store Body's costume

    // replace Body's costume with this one
    void Set(PBody pbody, bool fAllowDifferentShape = fFalse);
};

#endif // !Body_H
