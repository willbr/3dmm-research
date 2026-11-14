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

#include "three_mm/body_create.h"
#include "three_mm/body_edit.h"
#include "three_mm/body_runtime.h"
#include "three_mm/body_geometry.h"

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
    static BMTL *_pbmtlHilite; // hilight material
    BACT *_prgbact;      // array of BACTs
    PDynamicArray _pglibset;       // body part set IDs
    LegacyLong _cbset;         // count of body part sets
    PCustomMaterial_CMTL *_prgpcmtl;    // array of PCMTLs -- one per body part set
    LegacyLong _cbactPart;     // count of model body parts in body
    LegacyLong _cactHidden;    // for Show() / Hide()
    void *_pvRenderContext; // non-owning renderer binding, copied with the body
    void (*_pfnAttach)(void *, Body *);
    void (*_pfnChanged)(void *);
    ThreeMMBodyGeometry _geometry; // C-owned bounds and hit-search state

  protected:
    Body(void)
    {
        three_mm_body_geometry_init(&_geometry);
    } // can't instantiate directly; must use PbodyNew
    static ThreeMMBodyCreateServices _CreateServices(void);
    static ThreeMMBodyEditServices _EditServices(void);
    static ThreeMMBodyRuntimeServices _RuntimeServices(void);
    static ThreeMMBodyGeometryServices _GeometryServices(void);
    void _DestroyShape(void);
    bool _FInit(PDynamicArray pglibactPar, PDynamicArray pglibset);
    bool _FInitShape(PDynamicArray pglibactPar, PDynamicArray pglibset);
    PBACT _PbactRoot(void) // ptr to root BACT
    {
        return _prgbact;
    }
    PBACT _PbactHilite(void) // ptr to hilite BACT
    {
        return _prgbact + 1;
    }                            // skip root BACT
    PBACT _PbactPart(LegacyLong ipart) // ptr to ipart'th body part
    {
        return _prgbact + 1 + 1 + ipart;
    }                 // skip root and hilite BACTs
    LegacyLong _Cbact(void) // count in _prgbact
    {
        return 1 + 1 + _cbactPart;
    }                       // root, hilite, and body part BACTs
    LegacyLong _Ibset(LegacyLong ipart) // body part set that this part belongs to
    {
        return *(short *)_pglibset->QvGet(ipart);
    }
    void _RenderChanged(void)
    {
        if (_pfnChanged != pvNil)
            _pfnChanged(_pvRenderContext);
    }
    void _RemoveMaterial(LegacyLong ibset);

    // Callbacks from BRender:
    static int BR_CALLBACK _FFilterSearch(BACT *pbact, PBMDL pbmdl, PBMTL pbmtl, BVEC3 *pbvec3RayPos,
                                          BVEC3 *pbvec3RayDir, BRS dzpNear, BRS dzpFar, void *pvArg);
    static void _BactRendered(PBACT pbact, RC *prc);
    static void _PrepareToRender(PBACT pbact);
    static void _GetRc(PBACT pbact, RC *prc);

  public:
    static PBody PbodyNew(PDynamicArray pglibactPar, PDynamicArray pglibset);
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
    static int BR_CALLBACK _FFilter(BACT *pbact, PBMDL pbmdl, PBMTL pbmtl, BVEC3 *pbvec3RayPos, BVEC3 *pbvec3RayDir,
                                    BRS dzpNear, BRS dzpFar, void *pv);

    bool FChangeShape(PDynamicArray pglibactPar, PDynamicArray pglibset);
    // Binding lifetime must cover the body and any duplicates. Configure
    // while hidden; the core owns its actor tree, the adapter owns its host.
    void SetRenderCallbacks(void *context, void (*attach)(void *, Body *), void (*changed)(void *));

    void Show(void);
    void Hide(void);
    void Hilite(void);
    void Unhilite(void);

    static void SetHiliteColor(LegacyLong iclr);
    // Release after all bodies, before shutting down BRender.
    static void ReleaseSharedMaterial(void);

    void LocateOrient(BRS xr, BRS yr, BRS zr, BMAT34 *pbmat34);
    void SetPartModel(LegacyLong ibact, Model *pmodl);
    void SetPartMatrix(LegacyLong ibact, BMAT34 *pbmat34);
    void SetPartSetMtrl(LegacyLong ibset, Material_MTRL *pmtrl);
    void SetPartSetCmtl(CustomMaterial_CMTL *pcmtl);
    void GetPartSetMaterial(LegacyLong ibset, bool *pfMtrl, Material_MTRL **ppmtrl, CustomMaterial_CMTL **ppcmtl);
    LegacyLong Cbset()
    {
        return _cbset;
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
    ThreeMMBodyCostume _costume; // C-owned material references

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
