/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Masked Bitmap management code declarations.

***************************************************************************/
#ifndef MBMP_H
#define MBMP_H
#include "color.h"
#include "mm/collection_storage.h"
#include "mm/masked_bitmap.h"
class Region;
typedef Region *PRegion;
namespace KauaiPixels { struct Surface32; struct PackedSurface; struct Surface8; struct RleBitmap8; struct MaskedTransfer8; }

const FileType kftgBmp = 'BMP';

enum
{
    fmbmpNil = 0,
    fmbmpUpsideDown = 1,
    fmbmpMask = 2,
};

typedef MMMaskedOwner *PMaskedBitmapMBMP;

PMaskedBitmapMBMP NewMaskedBitmap(const byte *pixels, LegacyLong row, LegacyLong height, const RC *extract,
                                 LegacyLong x, LegacyLong y, byte transparent, LegacyUlong flags = fmbmpNil,
                                 byte fill = 0);
PMaskedBitmapMBMP ImportMaskedBitmap(PFilename, byte transparent = 0, LegacyLong x = 0, LegacyLong y = 0,
                                    LegacyUlong flags = fmbmpNil, byte fill = 0);
PMaskedBitmapMBMP ReadMaskedBitmap(PDataBlock);
PMaskedBitmapMBMP ReadMaskedBitmap(const MMReadStream *);
PMaskedBitmapMBMP FetchMaskedBitmap(PResourceCache, ChunkTagOrType, ChunkNumber);
int ReadCachedMaskedBitmap(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);

inline void ReleaseMaskedBitmap(PMaskedBitmapMBMP *bitmap)
{
    mm_masked_owner_release(*bitmap);
    *bitmap = nullptr;
}
#ifdef DEBUG
void AssertMaskedBitmap(PMaskedBitmapMBMP, bool allowNull = false);
void MarkMaskedBitmap(PMaskedBitmapMBMP);
#else
inline void AssertMaskedBitmap(PMaskedBitmapMBMP, bool = false) {}
inline void MarkMaskedBitmap(PMaskedBitmapMBMP) {}
#endif

LegacyLong MaskedBitmapSize(PMaskedBitmapMBMP);
bool WriteMaskedBitmap(PMaskedBitmapMBMP, PDataBlock);
void GetMaskedBitmapBounds(PMaskedBitmapMBMP, RC *);
bool MaskedBitmapContains(PMaskedBitmapMBMP, LegacyLong x, LegacyLong y);
void DrawMaskedBitmap(PMaskedBitmapMBMP, byte *pixels, LegacyLong row, LegacyLong height, LegacyLong x, LegacyLong y,
                      RC *clip = nullptr, PRegion region = nullptr);
void DrawMaskedBitmapMask(PMaskedBitmapMBMP, byte *pixels, LegacyLong row, LegacyLong height, LegacyLong x, LegacyLong y,
                          RC *clip = nullptr);
bool DrawMaskedBitmapPacked(PMaskedBitmapMBMP, const KauaiPixels::PackedSurface &, const RC &, const Color *,
                            LegacyLong colors, const RC *clip = nullptr, PRegion region = nullptr, bool andShrink = false);
bool DrawMaskedBitmap32(PMaskedBitmapMBMP, const KauaiPixels::Surface32 &, const RC &, const Color *,
                        LegacyLong colors, const RC *clip = nullptr, PRegion region = nullptr);
bool DrawMaskedBitmap8(PMaskedBitmapMBMP, const KauaiPixels::Surface8 &, const RC &,
                       const RC *clip = nullptr, PRegion region = nullptr);
bool DrawMaskedBitmap8(PMaskedBitmapMBMP, const KauaiPixels::Surface8 &, const RC &, const KauaiPixels::MaskedTransfer8 &,
                       const RC *clip = nullptr, PRegion region = nullptr, bool andShrink = false);

// reads a bitmap from the given file
bool FReadBitmap(Filename *pfni, byte **pprgb, PDynamicArray *ppglclr, LegacyLong *pdxp, LegacyLong *pdyp, bool *pfUpsideDown,
                 byte bTransparent = 0);

// writes a bitmap file
bool FWriteBitmap(Filename *pfni, byte *prgb, PDynamicArray pglclr, LegacyLong dxp, LegacyLong dyp, bool fUpsideDown = fTrue);

#endif //! MBMP_H
