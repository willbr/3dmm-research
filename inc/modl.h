/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/*************************************************************************

    modl.h: Model class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BaseCacheableObject ---> Model

*************************************************************************/
#ifndef MODL_H
#define MODL_H

using namespace BRender;

// Model on file:
struct ModelOnFile
{
    short bo;
    short osk;
    short cver; // count of vertices
    short cfac; // count of faces
    BRS rRadius;
    BRB brb; // bounds
    BVEC3 bvec3Pivot;
    //	br_vertex rgbrv[]; // vertices
    //	br_face rgbrf[]; // faces
};
typedef ModelOnFile *PModelOnFile;
const ByteOrderMask kbomModlf = 0x55fffff0;

/****************************************
    Model: a wrapper for BRender models
****************************************/
typedef class Model *PModel;
#define Model_PAR BaseCacheableObject
#define kclsModel 'MODL'
class Model : public Model_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    BMDL *_pbmdl; // BRender model data
  protected:
    Model(void)
    {
    }
    bool _FInit(PDataBlock pblck);
    bool _FPrelight(long cblit, BVEC3 *prgbvec3Light);

  public:
    static PModel PmodlNew(long cbrv, BRV *prgbrv, long cbrf, BRF *prgbrf);
    static bool FReadModl(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb);
    static PModel PmodlReadFromDat(Filename *pfni);
    static PModel PmodlFromBmdl(PBMDL pbmdl);
    ~Model(void);
    PBMDL Pbmdl(void)
    {
        return _pbmdl;
    }
    void AdjustTdfCharacter(void);
    bool FWrite(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno);

    BRS Dxr(void)
    {
        return _pbmdl->bounds.max.v[0] - _pbmdl->bounds.min.v[0];
    }
    BRS Dyr(void)
    {
        return _pbmdl->bounds.max.v[1] - _pbmdl->bounds.min.v[1];
    }
    BRS Dzr(void)
    {
        return _pbmdl->bounds.max.v[2] - _pbmdl->bounds.min.v[2];
    }
};

#endif TMPL_H
