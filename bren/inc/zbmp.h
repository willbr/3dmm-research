/*************************************************************************

    zbmp.h: Z-buffer Bitmap Class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BaseCacheableObject ---> ZBMP

*************************************************************************/
#ifndef ZBMP_H
#define ZBMP_H
#include "mm/depth_bitmap.h"

namespace BRender
{
const MMDepthOwnerHost *DepthBitmapHost();
MMDepthOwner *NewDepthBitmap(MMI32 width, MMI32 height, const void *initial = nullptr);
MMDepthOwner *ReadDepthBitmap(PDataBlock);
int ReadCachedDepthBitmap(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
bool WriteDepthBitmap(const MMDepthOwner *, const MMArchiveWriter &, ChunkTagOrType, ChunkNumber *);
#ifdef DEBUG
void AssertDepthBitmap(const MMDepthOwner *);
#endif
} // namespace BRender

#endif
