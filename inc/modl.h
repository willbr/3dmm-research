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
static_assert(sizeof(ModelOnFile) == 48, "ModelOnFile on-disk layout drift");
typedef ModelOnFile *PModelOnFile;
const ByteOrderMask kbomModlf = 0x55fffff0;

// Wire-format face entry. Mirrors br_face from bren/inc/model.h with one
// difference: the runtime br_face has a br_material* slot which is 4 bytes
// on x86 but 8 bytes on x64. The 1995 .CHK chunks store the material slot
// as a 4-byte placeholder (loaded as nil at runtime), so the on-disk size
// is fixed at 32 bytes regardless of architecture. Model::_FInit reads
// this struct from the chunk and expands each entry into a runtime br_face
// (32 bytes on x86, 36 bytes on x64). Note: br_fraction in the BASED_FIXED
// build is `short` (2 bytes), not int32 -- so n[3] is 6 bytes, not 12.
#pragma pack(push, 4)
struct BrFaceOnFile
{
    uint16_t vertices[3];   // br_uint_16 vertices[3]
    uint16_t edges[3];      // br_uint_16 edges[3]
    uint32_t material_slot; // 32-bit placeholder for br_material*
    uint16_t smoothing;     // br_uint_16
    uint8_t flags;          // br_uint_8
    uint8_t _pad0;          // br_uint_8
    int16_t n[3];           // br_fvector3 = 3 * br_fraction (short)
    // 2 bytes implicit pad here (align d to 4)
    int32_t d;              // br_scalar (long)
};
#pragma pack(pop)
static_assert(sizeof(BrFaceOnFile) == 32, "BrFaceOnFile on-disk layout drift");

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
