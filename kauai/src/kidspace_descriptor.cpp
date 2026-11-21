/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kidspace_descriptor.h"
#include "handle_services.h"
ASSERTNAME
RTCLASS(KidspaceGraphicObjectDescriptor)
RTCLASS(KidspaceGraphicObjectDescriptorLocation)

int KidspaceGraphicObjectDescriptorLocation::FReadGkds(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block,
                                                      void **out, MMI32 *bytes)
{
    *bytes = static_cast<MMI32>(block->view.length);
    if (!out)
        return fTrue;
    if (mm_owned_block_code(block, 0, nullptr, KauaiDecodeBlockHandle, nullptr) != MM_DATA_OK)
        return fFalse;
    *bytes = static_cast<MMI32>(block->view.length);
    MMBlockIO io;
    MMReadStream stream;
    if (mm_owned_block_io(block, &io) != MM_DATA_OK || mm_block_io_stream(&io, &stream) != MM_DATA_OK)
        return fFalse;
    auto descriptor = NewObj KidspaceGraphicObjectDescriptorLocation;
    if (!descriptor)
        return fFalse;
    if (!mm_kidspace_read(&descriptor->_descriptor, &stream, KauaiCopyAllocator()))
    {
        delete descriptor;
        return fFalse;
    }
    *out = descriptor;
    return fTrue;
}

KidspaceGraphicObjectDescriptorLocation::~KidspaceGraphicObjectDescriptorLocation(void)
{
    mm_kidspace_dispose(&_descriptor);
}

#ifdef DEBUG
void KidspaceGraphicObjectDescriptorLocation::AssertValid(LegacyUlong grf)
{
    KidspaceGraphicObjectDescriptorLocation_PAR::AssertValid(grf);
    Assert(_descriptor.locations > 0 && _descriptor.storage.buffers[0].data, "missing Kidspace descriptor data");
}

void KidspaceGraphicObjectDescriptorLocation::MarkMem(void)
{
    AssertValid(0);
    KidspaceGraphicObjectDescriptorLocation_PAR::MarkMem();
    MarkPv(_descriptor.storage.buffers[0].data);
}
#endif

long KidspaceGraphicObjectDescriptorLocation::Gokk(void)
{
    AssertThis(0);
    return _descriptor.kind;
}

bool KidspaceGraphicObjectDescriptorLocation::FGetCume(ulong grfcust, long sno, CursorMapEntry *pcume)
{
    AssertThis(0);
    AssertVarMem(pcume);
    if (mm_kidspace_cursor(&_descriptor, static_cast<MMU32>(grfcust), static_cast<MMU32>(sno), pcume))
        return fTrue;
    if (_descriptor.cursors)
        TrashVar(pcume);
    return fFalse;
}

void KidspaceGraphicObjectDescriptorLocation::GetLop(long hidPar, LOP *plop)
{
    AssertThis(0);
    AssertVarMem(plop);
    const auto parent = static_cast<MMI32>(hidPar);
    mm_kidspace_location(&_descriptor, parent == hidPar ? parent : 0, plop);
}
