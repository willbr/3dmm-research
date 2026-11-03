/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// Original indexed 2x stretch algorithms, independent of windows and ports.
#include "pixel_stretch.h"
#include "kauai_core.h"
#include "region.h"
ASSERTNAME

/***************************************************************************
    This does a 2x stretch blt, clipped to prcClip and pregnClip. The
    clipping is expressed in destination coordinates.
***************************************************************************/
void DoubleStretch(byte *prgbSrc, long cbRowSrc, long dypSrc, RC *prcSrc, byte *prgbDst, long cbRowDst, long dypDst,
                   long xpDst, long ypDst, RC *prcClip, PRegion pregnClip)
{
    AssertPvCb(prgbSrc, LwMul(cbRowSrc, dypSrc));
    AssertPvCb(prgbDst, LwMul(cbRowDst, dypDst));
    AssertVarMem(prcSrc);
    Assert(prcSrc->xpLeft >= 0 && prcSrc->ypTop >= 0 && prcSrc->xpRight <= cbRowSrc && prcSrc->ypBottom <= dypSrc,
           "Source rectangle not in source bitmap!");
    AssertNilOrVarMem(prcClip);
    AssertNilOrPo(pregnClip, 0);

    long xpOn, xpOff, dypAdvance, dxpBase, yp;
    bool fSecondRow;
    RegionScanner regsc;
    RC rcT(xpDst, ypDst, xpDst + 2 * prcSrc->Dxp(), ypDst + 2 * prcSrc->Dyp());
    RC rcClip(0, 0, cbRowDst, dypDst);

    if (!rcClip.FIntersect(&rcT))
        return;
    if (pvNil != prcClip && !rcClip.FIntersect(prcClip))
        return;

    // Set up the region scanner
    if (pvNil != pregnClip)
        regsc.Init(pregnClip, &rcClip);
    else
        regsc.InitRc(&rcClip, &rcClip);
    dxpBase = rcClip.xpLeft - xpDst;

    // move to rcClip.ypTop
    yp = rcClip.ypTop;
    prgbSrc += prcSrc->xpLeft + LwMul(prcSrc->ypTop + ((yp - ypDst) >> 1), cbRowSrc);
    prgbDst += xpDst + LwMul(yp, cbRowDst);
    fSecondRow = (yp - ypDst) & 1;

    for (;;)
    {
        if (klwMax == (xpOn = regsc.XpCur()))
        {
            // empty strip of the region
            dypAdvance = regsc.DypCur();
            goto LAdvance;
        }
        xpOn += dxpBase;
        if (fSecondRow || (regsc.DypCur() == 1))
            goto LOneRow;

        // copy two rows to the destination
        for (;;)
        {
            xpOff = regsc.XpFetch() + dxpBase;
            AssertIn(xpOff - 1, xpOn, rcClip.Dxp() + dxpBase);

#ifdef IN_80386
// copy two rows to the destination in native
#define pbDstReg edi
#define pbSrcReg esi
#define pbDst2Reg ebx
#define lwTReg edx

            __asm {
                // lwTReg = xpOn;
                // pbDstReg = prgbDst + xpOn;
                // pbSrcReg = prgbSrc + (xpOn >> 1);
                // pbDst2Reg = pbDstReg + cbRowDst;
				mov		lwTReg,xpOn
				mov		pbSrcReg,lwTReg
				mov		pbDstReg,lwTReg
				sar		pbSrcReg,1
				add		pbDstReg,prgbDst
				add		pbSrcReg,prgbSrc
				mov		pbDst2Reg,pbDstReg
				add		pbDst2Reg,cbRowDst

                    // if (!(lwTReg & 1)) goto LGetCount2;
				test	lwTReg,1
				mov		ecx,xpOff
				jz		LGetCount2

                    // move the single leading byte
                    // lwTReg++;
                    // al = *pbSrcReg++;
                    // *pbDstReg++ = al;
                    // *pbDstReg2++ = al;
				inc		lwTReg
				mov		al,[pbSrcReg]
				inc		pbSrcReg
				mov		[pbDstReg],al
				inc		pbDstReg
				mov		[pbDst2Reg],al
				inc		pbDst2Reg

LGetCount2:
                    // ecx = xpOff - lwTReg;
                    // ecx >>= 1;
                    // if (ecx <= 0) goto LLastByte2;
				sub		ecx,lwTReg
				sar		ecx,1
				jle		LLastByte2

LLoop2:
                    // al = *pbSrcReg++;
                    // ah = al;
                    // *(short *)pbDstReg = ax;
                    // *(short *)pbDst2Reg = ax;
                    // pbDstReg += 2;
                    // pbDst2Reg += 2;
				mov		al,[pbSrcReg]
				inc		pbSrcReg
				mov		ah,al
				mov		[pbDstReg],ax
				add		pbDstReg,2
				mov		[pbDst2Reg],ax
				add		pbDst2Reg,2

                // if (--ecx != 0) goto LLoop2;
				loop	LLoop2

LLastByte2:
                    // if (!(xpOff & 1)) goto LDone2;
				test	xpOff,1
				jz		LDone2

                    // move the single trailing byte
                    // al = *pbSrcReg;
                    // *pbDstReg = al;
                    // *pbDstReg2 = al;
				mov		al,[pbSrcReg]
				mov		[pbDstReg],al
				mov		[pbDst2Reg],al
LDone2:
            }

#undef pbDstReg
#undef pbSrcReg
#undef pbDst2Reg
#undef lwTReg
#else  //! IN_80386
       // copy two rows to the destination in C code
            byte *pbSrc = prgbSrc + (xpOn >> 1);
            byte *pbDst = prgbDst + xpOn;
            byte *pbDst2 = pbDst + cbRowDst;
            byte bT;
            long cactLoop;

            if (xpOn & 1)
            {
                // do leading single byte
                bT = *pbSrc++;
                *pbDst++ = bT;
                *pbDst2++ = bT;
                xpOn++;
            }

            for (cactLoop = (xpOff - xpOn) >> 1; cactLoop > 0; cactLoop--)
            {
                bT = *pbSrc++;
                pbDst[0] = bT;
                pbDst[1] = bT;
                pbDst += 2;
                pbDst2[0] = bT;
                pbDst2[1] = bT;
                pbDst2 += 2;
            }

            if (xpOff & 1)
            {
                // do the trailing byte
                bT = *pbSrc;
                *pbDst = bT;
                *pbDst2 = bT;
            }
#endif //! IN_80386

            if (klwMax == (xpOn = regsc.XpFetch())) break;
            xpOn += dxpBase;
            AssertIn(xpOn - 1, xpOff, rcClip.Dxp() - 1 + dxpBase);
        }
        dypAdvance = 2;
        goto LAdvance;

    LOneRow:
        // copy just one row to the destination
        for (;;)
        {
            xpOff = regsc.XpFetch() + dxpBase;
            AssertIn(xpOff - 1, xpOn, rcClip.Dxp() + dxpBase);

#ifdef IN_80386
// copy just one row to the destination in native
#define pbDstReg edi
#define pbSrcReg esi
#define lwTReg edx

            __asm {
                // lwTReg = xpOn;
                // pbDstReg = prgbDst + xpOn;
                // pbSrcReg = prgbSrc + (xpOn >> 1);
				mov		lwTReg,xpOn
				mov		pbSrcReg,lwTReg
				mov		pbDstReg,lwTReg
				sar		pbSrcReg,1
				add		pbDstReg,prgbDst
				add		pbSrcReg,prgbSrc

                    // if (!(lwTReg & 1)) goto LGetCount1;
				test	lwTReg,1
				mov		ecx,xpOff
				jz		LGetCount1

                    // move the single leading byte
                    // lwTReg++;
                    // al = *pbSrcReg++;
                    // *pbDstReg++ = al;
				inc		lwTReg
				mov		al,[pbSrcReg]
				inc		pbSrcReg
				inc		pbDstReg
				mov		[pbDstReg-1],al

LGetCount1:
                    // ecx = xpOff - lwTReg;
                    // ecx >>= 1;
                    // if (ecx <= 0) goto LLastByte1;
				sub		ecx,lwTReg
				sar		ecx,1
				jle		LLastByte1

LLoop1:
                    // al = *pbSrcReg++;
                    // ah = al;
                    // *(short *)pbDstReg = ax;
                    // pbDstReg += 2;
				mov		al,[pbSrcReg]
				inc		pbSrcReg
				add		pbDstReg,2
				mov		ah,al
				mov		[pbDstReg-2],ax

                    // if (--ecx != 0) goto LLoop1;
				loop	LLoop1

LLastByte1:
                    // if (!(xpOff & 1)) goto LDone1;
				test	xpOff,1
				jz		LDone1

                    // move the single trailing byte
                    // al = *pbSrcReg;
                    // *pbDstReg = al;
				mov		al,[pbSrcReg]
				mov		[pbDstReg],al
LDone1:
            }

#undef pbDstReg
#undef pbSrcReg
#undef lwTReg
#else  //! IN_80386
       // copy just one row to the destination in C code
            byte bT;
            byte *pbSrc = prgbSrc + (xpOn >> 1);
            byte *pbDst = prgbDst + xpOn;
            long cactLoop;

            if (xpOn & 1)
            {
                // do leading single byte
                *pbDst++ = *pbSrc++;
                xpOn++;
            }

            for (cactLoop = (xpOff - xpOn) >> 1; cactLoop > 0; cactLoop--)
            {
                bT = *pbSrc++;
                pbDst[0] = bT;
                pbDst[1] = bT;
                pbDst += 2;
            }

            if (xpOff & 1)
            {
                // do the trailing byte
                *pbDst = *pbSrc;
            }
#endif //! IN_80386

            if (klwMax == (xpOn = regsc.XpFetch())) break;
            xpOn += dxpBase;
            AssertIn(xpOn - 1, xpOff, rcClip.Dxp() - 1 + dxpBase);
        }
        dypAdvance = 1;

    LAdvance:
        if ((yp += dypAdvance) >= rcClip.ypBottom)
            break;
        regsc.ScanNext(dypAdvance);
        prgbDst += dypAdvance * cbRowDst;
        prgbSrc += (dypAdvance >> 1) * cbRowSrc;
        if (dypAdvance & 1)
        {
            if (fSecondRow)
                prgbSrc += cbRowSrc;
            fSecondRow = !fSecondRow;
        }
    }
}

/***************************************************************************
    This does a 2x vertical and 1x horizontal stretch blt, clipped to prcClip
    and pregnClip. The clipping is expressed in destination coordinates.
***************************************************************************/
void DoubleVertStretch(byte *prgbSrc, long cbRowSrc, long dypSrc, RC *prcSrc, byte *prgbDst, long cbRowDst, long dypDst,
                       long xpDst, long ypDst, RC *prcClip, PRegion pregnClip)
{
    AssertPvCb(prgbSrc, LwMul(cbRowSrc, dypSrc));
    AssertPvCb(prgbDst, LwMul(cbRowDst, dypDst));
    AssertVarMem(prcSrc);
    Assert(prcSrc->xpLeft >= 0 && prcSrc->ypTop >= 0 && prcSrc->xpRight <= cbRowSrc && prcSrc->ypBottom <= dypSrc,
           "Source rectangle not in source bitmap!");
    AssertNilOrVarMem(prcClip);
    AssertNilOrPo(pregnClip, 0);

    long xpOn, xpOff, dypAdvance, dxpBase, yp;
    bool fSecondRow;
    RegionScanner regsc;
    RC rcT(xpDst, ypDst, xpDst + prcSrc->Dxp(), ypDst + 2 * prcSrc->Dyp());
    RC rcClip(0, 0, cbRowDst, dypDst);

    if (!rcClip.FIntersect(&rcT))
        return;
    if (pvNil != prcClip && !rcClip.FIntersect(prcClip))
        return;

    // Set up the region scanner
    if (pvNil != pregnClip)
        regsc.Init(pregnClip, &rcClip);
    else
        regsc.InitRc(&rcClip, &rcClip);
    dxpBase = rcClip.xpLeft - xpDst;

    // move to rcClip.ypTop
    yp = rcClip.ypTop;
    prgbSrc += prcSrc->xpLeft + LwMul(prcSrc->ypTop + ((yp - ypDst) >> 1), cbRowSrc);
    prgbDst += xpDst + LwMul(yp, cbRowDst);
    fSecondRow = (yp - ypDst) & 1;

    for (;;)
    {
        if (klwMax == (xpOn = regsc.XpCur()))
        {
            // empty strip of the region
            dypAdvance = regsc.DypCur();
            goto LAdvance;
        }
        xpOn += dxpBase;
        if (fSecondRow || (regsc.DypCur() == 1))
            goto LOneRow;

        // copy two rows to the destination
        for (;;)
        {
            xpOff = regsc.XpFetch() + dxpBase;
            AssertIn(xpOff - 1, xpOn, rcClip.Dxp() + dxpBase);

#ifdef IN_80386

            // copy two rows to the destination in native
            __asm
            {
                // edi = prgbDst + xpOn;
                // esi = eax = prgbSrc + xpOn;
                // ebx = pbDstReg + cbRowDst;
                // ecx = xpOff - xpOn
				mov		edx,xpOn
				mov		ecx,xpOff
				mov		edi,edx
				mov		esi,edx
				sub		ecx,edx
				add		edi,prgbDst
				add		esi,prgbSrc
				mov		ebx,edi
				mov		eax,esi
				add		ebx,cbRowDst

                    // copy the first row
				mov		edx,ecx
				shr		ecx,2
				rep		movsd
				mov		ecx,edx
				and		ecx,3
				rep		movsb

                    // prepare to copy the second row
				mov		esi,eax
				mov		edi,ebx
				mov		ecx,edx

                    // copy the second row
				shr		ecx,2
				and		edx,3
				rep		movsd
				mov		ecx,edx
				rep		movsb
            }

#else //! IN_80386

            // copy two rows to the destination in C code
            CopyPb(prgbSrc + xpOn, prgbDst + xpOn, xpOff - xpOn);
            CopyPb(prgbSrc + xpOn, prgbDst + xpOn + cbRowDst, xpOff - xpOn);

#endif //! IN_80386

            if (klwMax == (xpOn = regsc.XpFetch())) break;
            xpOn += dxpBase;
            AssertIn(xpOn - 1, xpOff, rcClip.Dxp() - 1 + dxpBase);
        }
        dypAdvance = 2;
        goto LAdvance;

    LOneRow:
        // copy just one row to the destination
        for (;;)
        {
            xpOff = regsc.XpFetch() + dxpBase;
            AssertIn(xpOff - 1, xpOn, rcClip.Dxp() + dxpBase);

#ifdef IN_80386

            // copy one row to the destination in native
            __asm
            {
                // edi = prgbDst + xpOn;
                // esi = prgbSrc + xpOn;
                // ecx = xpOff - xpOn
				mov		edx,xpOn
				mov		ecx,xpOff
				mov		edi,edx
				mov		esi,edx
				sub		ecx,edx
				add		edi,prgbDst
				add		esi,prgbSrc

                    // copy the row
				mov		edx,ecx
				shr		ecx,2
				and		edx,3
				rep		movsd
				mov		ecx,edx
				rep		movsb
            }

#else //! IN_80386

            // copy one row to the destination in C code
            CopyPb(prgbSrc + xpOn, prgbDst + xpOn, xpOff - xpOn);

#endif //! IN_80386

            if (klwMax == (xpOn = regsc.XpFetch())) break;
            xpOn += dxpBase;
            AssertIn(xpOn - 1, xpOff, rcClip.Dxp() - 1 + dxpBase);
        }
        dypAdvance = 1;

    LAdvance:
        if ((yp += dypAdvance) >= rcClip.ypBottom)
            break;
        regsc.ScanNext(dypAdvance);
        prgbDst += dypAdvance * cbRowDst;
        prgbSrc += (dypAdvance >> 1) * cbRowSrc;
        if (dypAdvance & 1)
        {
            if (fSecondRow)
                prgbSrc += cbRowSrc;
            fSecondRow = !fSecondRow;
        }
    }
}
