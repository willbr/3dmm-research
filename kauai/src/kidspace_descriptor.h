/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_KIDSPACE_DESCRIPTOR_H
#define KAUAI_KIDSPACE_DESCRIPTOR_H
#include "kauai_core.h"
#include "framedef.h"
using namespace Chunky;
// location from parent map structure
struct LOP
{
    LegacyLong hidPar;
    LegacyLong xp;
    LegacyLong yp;
    LegacyLong zp; // the z-plane number used for placing the KidspaceGraphicObject in the GraphicsObject tree
};

// cursor map entry
struct CursorMapEntry
{
    LegacyUlong grfcustMask; // what cursor states this CursorMapEntry is good for
    LegacyUlong grfcust;
    LegacyUlong grfbitSno;   // what button states this CursorMapEntry is good for
    ChunkNumber cnoCurs;     // the cursor to use
    ChildChunkID chidScript; // execution script (absolute)
    LegacyLong cidDefault;   // default command
    ChunkNumber cnoTopic;    // tool tip topic
};

typedef class KidspaceGraphicObjectDescriptor *PKidspaceGraphicObjectDescriptor;
#define KidspaceGraphicObjectDescriptor_PAR BaseCacheableObject
#define kclsKidspaceGraphicObjectDescriptor 'GOKD'
class KidspaceGraphicObjectDescriptor : public KidspaceGraphicObjectDescriptor_PAR
{
    RTCLASS_DEC

  protected:
    KidspaceGraphicObjectDescriptor(void)
    {
    }

  public:
    virtual long Gokk(void) = 0;
    virtual bool FGetCume(ulong grfcust, long sno, CursorMapEntry *pcume) = 0;
    virtual void GetLop(long hidPar, LOP *plop) = 0;
};

/***************************************************************************
    Standard KidspaceGraphicObject descriptor. Contains location information and cursor
    map stuff.
***************************************************************************/
// KidspaceGraphicObject construction descriptor on file - these are stored in chunky resource files
struct GOKDF
{
    short bo;
    short osk;
    LegacyLong gokk;
    // LOP rglop[];		ends with a default entry (hidPar == hidNil)
    // CursorMapEntry rgcume[];	the cursor map
};
const ByteOrderMask kbomGokdf = 0x0C000000;
static_assert(sizeof(GOKDF) == 8, "GOK descriptor header must retain its stored size");
static_assert(sizeof(LOP) == 16, "GOK location entries must retain their stored size");
static_assert(sizeof(CursorMapEntry) == 28, "GOK cursor entries must retain their stored size");

typedef class KidspaceGraphicObjectDescriptorLocation *PKidspaceGraphicObjectDescriptorLocation;
#define KidspaceGraphicObjectDescriptorLocation_PAR KidspaceGraphicObjectDescriptor
#define kclsKidspaceGraphicObjectDescriptorLocation 'GKDS'
class KidspaceGraphicObjectDescriptorLocation : public KidspaceGraphicObjectDescriptorLocation_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    HQ _hqData;
    long _gokk;
    long _clop;
    long _ccume;

    KidspaceGraphicObjectDescriptorLocation(void)
    {
    }

  public:
    // An object reader for a KidspaceGraphicObjectDescriptor.
    static bool FReadGkds(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, DataBlock *pblck,
                          PBaseCacheableObject *ppbaco, LegacyLong *pcb);
    ~KidspaceGraphicObjectDescriptorLocation(void);

    virtual long Gokk(void);
    virtual bool FGetCume(ulong grfcust, long sno, CursorMapEntry *pcume);
    virtual void GetLop(long hidPar, LOP *plop);
};

#endif
