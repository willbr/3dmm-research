/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Data movement declarations

***************************************************************************/
#ifndef UTILCOPY_H
#define UTILCOPY_H

void BltPb(void *pv1, void *pv2, LegacyLong cb);
void CopyPb(void *pv1, void *pv2, LegacyLong cb);
void ClearPb(void *pv, LegacyLong cb);
void FillPb(void *pv1, LegacyLong cb, byte b);
void ReversePb(void *pv, LegacyLong cb);
void ReverseRgsw(void *pv, LegacyLong csw);
void ReverseRglw(void *pv, LegacyLong clw);
void SwapBlocks(void *pv, LegacyLong cb1, LegacyLong cb2);
void SwapPb(void *pv1, void *pv2, LegacyLong cb);
void MoveElement(void *prgv, LegacyLong cbElement, LegacyLong ivSrc, LegacyLong ivTarget);
bool FEqualRgb(void *pv1, void *pv2, LegacyLong cb);
LegacyLong CbEqualRgb(void *pv1, void *pv2, LegacyLong cbMax);
LegacyUlong FcmpCompareRgb(void *pv1, void *pv2, LegacyLong cb);

#ifdef DEBUG
#define SwapVars(pv1, pv2)                                                                                             \
    if (size(*pv1) != size(*pv2))                                                                                      \
        Bug("sizes don't match");                                                                                      \
    else                                                                                                               \
        SwapPb(pv1, pv2, size(*pv1))
#else //! DEBUG
#define SwapVars(pv1, pv2) SwapPb(pv1, pv2, size(*pv1))
#endif //! DEBUG

#endif // UTILCOPY_H
