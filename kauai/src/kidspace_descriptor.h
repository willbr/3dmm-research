/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_KIDSPACE_DESCRIPTOR_H
#define KAUAI_KIDSPACE_DESCRIPTOR_H
#include "kauai_core.h"
#include "framedef.h"
#include "mm/studio_resource.h"
using namespace Chunky;
using LOP = MMKidspaceLocation;
using CursorMapEntry = MMKidspaceCursor;

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
typedef class KidspaceGraphicObjectDescriptorLocation *PKidspaceGraphicObjectDescriptorLocation;
#define KidspaceGraphicObjectDescriptorLocation_PAR KidspaceGraphicObjectDescriptor
#define kclsKidspaceGraphicObjectDescriptorLocation 'GKDS'
class KidspaceGraphicObjectDescriptorLocation : public KidspaceGraphicObjectDescriptorLocation_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    MMKidspaceDescriptor _descriptor = {};
    KidspaceGraphicObjectDescriptorLocation() = default;

  public:
    // An object reader for a KidspaceGraphicObjectDescriptor.
    static int FReadGkds(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **, MMI32 *);
    ~KidspaceGraphicObjectDescriptorLocation(void);

    virtual long Gokk(void);
    virtual bool FGetCume(ulong grfcust, long sno, CursorMapEntry *pcume);
    virtual void GetLop(long hidPar, LOP *plop);
};

#endif
