/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Standard Kauai codec.

    This codec compresses a byte stream into a bit stream. The bit stream
    is decoded by the following pseudo code:

        loop forever
            if (next bit is 0)
                // literal byte
                write next 8 bits
                continue

            // initialize length
            length = 1

            // find the offset
            if (next bit is 0)
                offset = next 6 bits + 0x01
            else if (next bit is 0)
                offset = next 9 bits + 0x41
            else if (next bit is 0)
                offset = next 12 bits + 0x0241
            else
                offset = next 20 bits
                if (offset is 0xFFFFF)
                    break // we're done
                offset += 0x1241
                length++

            // get the length
            c = number of non-zero bits (up to 11)
            skip the zero bit
            length += next c bits + (1 << c)

            // write the destination bytes
            write length bytes from the current dest position minus offset.

    Essentially, previous uncompressed byte strings form a dictionary for
    the compression.

    As an example, the byte string consisting of 37 a's in a row would be
    compressed as:

        // first a encoded:
        0 			// literal byte
        01100001 	// 'a' = 0x61

        // next 36 a's encoded:
        01			// 6 bit offset
        000000		// offset = 0 + 1 = 1
        011111		// length encoded in next 5 bits
        00011		// length = 1 + 3 + (1 << 5) = 36

        1111		// 20 bit offset
        11111111111111111111	// special 20 bit offset indicating end of data

    The source stream is 296 bits long. The compressed stream is 28 bits (not
    counting the 24 bit termination).

    For a less trivial example: the string abaabaaabaaaab would be compressed
    to the following bit stream:

        // first aba encoded:
        0 			// literal byte
        01100001 	// 'a' = 0x61
        0			// literal byte
        01100010	// 'b = 0x62
        0 			// literal byte
        01100001 	// 'a' = 0x61

        // next abaa encoded:
        01			// 6 bit offset
        000010		// offset = 2 + 1 = 3
        01			// length encoded in next 1 bit
        1			// length = 1 + 1 + (1 << 1) = 4

        // next abaaa encoded:
        01			// 6 bit offset
        000011		// offset = 3 + 1 = 4
        011			// length encoded in next 2 bits
        00			// length = 1 + 0 + (1 << 2) = 5

        // next ab encoded:
        01			// 6 bit offset
        000100		// offset = 4 + 1 = 5
        0			// length encoded in next 0 bits
                    // length = 1 + 0 + (1 << 0) = 2

        1111		// 20 bit offset
        11111111111111111111	// special 20 bit offset indicating end of data

    The uncompressed stream is 112 bits. The compressed stream is 60 bits (not
    including the 24 bit termination).

    All compressed streams are padded with extra 0xFF bytes so that at
    decompression time we can verify a priori that decompression will
    terminate (by verifying that there are at least 6 0xFF bytes at the end.

***************************************************************************/
#include "util.h"
#include "three_mm/codec.h"
ASSERTNAME
RTCLASS(KauaiCodec)

namespace {
void *AllocateCodec(void *, void *, ThreeMMU32, ThreeMMU32 bytes, int priority)
{
    Assert(priority == THREE_MM_STORAGE_NORMAL, "bad codec allocation priority");
    void *result = nullptr;
    return FAllocPv(&result, bytes, fmemNil, mprNormal) ? result : nullptr;
}
void ReleaseCodec(void *, void *data, ThreeMMU32) { FreePpv(&data); }
}

bool KauaiCodec::FConvert(bool fEncode, LegacyLong cfmt, void *pvSrc, LegacyLong cbSrc,
                         void *pvDst, LegacyLong cbDst, LegacyLong *pcbDst)
{
    AssertThis(0);
    AssertIn(cbSrc, 1, kcbMax);
    AssertPvCb(pvSrc, cbSrc);
    AssertIn(cbDst, 1, kcbMax);
    AssertPvCb(pvDst, cbDst);
    AssertVarMem(pcbDst);
    if (!FCanDo(fEncode, cfmt)) return false;
    TrashVar(pcbDst);
    ThreeMMI32 count;
    const ThreeMMStorageAllocator allocator = {nullptr, AllocateCodec, ReleaseCodec};
    const int result = fEncode ? three_mm_codec_encode(cfmt, pvSrc, cbSrc, pvDst, cbDst, &count, &allocator) :
                                three_mm_codec_decode(cfmt, pvSrc, cbSrc, pvDst, cbDst, &count);
    if (result == THREE_MM_DATA_OK) *pcbDst = count;
    else if (!fEncode) Bug("bad compressed data");
    else if (result == THREE_MM_DATA_MEMORY) Warn("failed to allocate memory for links");
    return result == THREE_MM_DATA_OK;
}
