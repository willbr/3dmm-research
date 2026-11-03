/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_PIXEL_STRETCH_H
#define KAUAI_PIXEL_STRETCH_H
#include <cstdint>
class RC;
class Region;
// stretch by a factor of 2 in each dimension.
void DoubleStretch(std::uint8_t *prgbSrc, long cbRowSrc, long dypSrc, RC *prcSrc, std::uint8_t *prgbDst, long cbRowDst,
                   long dypDst, long xpDst, long ypDst, RC *prcClip, Region *pregnClip);

// stretch by a factor of 2 in vertical direction only.
void DoubleVertStretch(std::uint8_t *prgbSrc, long cbRowSrc, long dypSrc, RC *prcSrc, std::uint8_t *prgbDst,
                       long cbRowDst, long dypDst, long xpDst, long ypDst, RC *prcClip, Region *pregnClip);

#endif
