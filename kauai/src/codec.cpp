/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Codec manager class.

***************************************************************************/
#include "util.h"
#include "handle_services.h"
ASSERTNAME

RTCLASS(CodecManager)
RTCLASS(Codec)

/***************************************************************************
    Constructor for the compression manager. pcodc is an optional default
    codec. cfmt is the default compression format.
***************************************************************************/
CodecManager::CodecManager(PCodec pcodc, LegacyLong cfmt)
{
    AssertNilOrPo(pcodc, 0);
    Assert(cfmtNil != cfmt, "nil default compression format");

    _cfmtDef = cfmt;
    _pcodcDef = pcodc;
    if (pvNil != _pcodcDef)
        _pcodcDef->AddRef();
    _pglpcodc = pvNil;

    AssertThis(0);
}

/***************************************************************************
    Destructor for the compression manager.
***************************************************************************/
CodecManager::~CodecManager(void)
{
    AssertThis(0);

    ReleasePpo(&_pcodcDef);
    if (pvNil != _pglpcodc)
    {
        LegacyLong ipcodc;
        PCodec pcodc;

        for (ipcodc = _pglpcodc->IvMac(); ipcodc-- > 0;)
        {
            _pglpcodc->Get(ipcodc, &pcodc);
            ReleasePpo(&pcodc);
        }
        ReleasePpo(&_pglpcodc);
    }
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a CodecManager.
***************************************************************************/
void CodecManager::AssertValid(LegacyUlong grf)
{
    CodecManager_PAR::AssertValid(0);
    Assert(cfmtNil != _cfmtDef, "nil default compression");
    AssertNilOrPo(_pcodcDef, 0);
    AssertNilOrPo(_pglpcodc, 0);
}

/***************************************************************************
    Mark memory for the CodecManager.
***************************************************************************/
void CodecManager::MarkMem(void)
{
    AssertValid(0);
    CodecManager_PAR::MarkMem();

    MarkMemObj(_pcodcDef);
    if (pvNil != _pglpcodc)
    {
        LegacyLong ipcodc;
        PCodec pcodc;

        for (ipcodc = _pglpcodc->IvMac(); ipcodc-- > 0;)
        {
            _pglpcodc->Get(ipcodc, &pcodc);
            MarkMemObj(pcodc);
        }
        MarkMemObj(_pglpcodc);
    }
}
#endif // DEBUG

/***************************************************************************
    Set the default compression type.
***************************************************************************/
void CodecManager::SetCfmtDefault(LegacyLong cfmt)
{
    AssertThis(0);

    if (cfmt == cfmtNil)
    {
        Bug("can't set default compression to nil");
        return;
    }

    _cfmtDef = cfmt;
}

/***************************************************************************
    Add a codec to the compression manager.
***************************************************************************/
bool CodecManager::FRegisterCodec(PCodec pcodc)
{
    AssertThis(0);
    AssertPo(pcodc, 0);

    if (pvNil == _pglpcodc && pvNil == (_pglpcodc = DynamicArray::PglNew(size(PCodec))))
        return fFalse;

    if (!_pglpcodc->FAdd(&pcodc))
        return fFalse;

    pcodc->AddRef();
    return fTrue;
}

/***************************************************************************
    Return whether we can encode or decode the given format.
***************************************************************************/
bool CodecManager::FCanDo(LegacyLong cfmt, bool fEncode)
{
    AssertThis(0);
    PCodec pcodc;

    if (cfmtNil == cfmt)
        return fFalse;

    return _FFindCodec(fEncode, cfmt, &pcodc);
}

/***************************************************************************
    Gets the type of compression used on the block (assuming it is
    compressed).
***************************************************************************/
namespace {
int ReadPackedHeader(void *context, ThreeMMU32 offset, void *bytes, size_t count)
{
    if (offset > 0x7fffffffUL || count > 0x7fffffffUL) return 0;
    return static_cast<PDataBlock>(context)->FReadRgb(bytes, static_cast<LegacyLong>(count), offset, true);
}
} // namespace
void *CodecManager::_FindDecoder(void *context, ThreeMMI32 format)
{
    PCodec codec = nullptr;
    if (format == cfmtNil) return nullptr;
    return static_cast<CodecManager *>(context)->_FFindCodec(false, format, &codec) ? codec : nullptr;
}
int CodecManager::_ConvertDecoder(void *codec, ThreeMMI32 format, const void *input, ThreeMMU32 bytes,
                                  void *output, ThreeMMU32 capacity, ThreeMMI32 *written)
{
    LegacyLong count = *written;
    const bool ok = static_cast<PCodec>(codec)->FConvert(false, format, const_cast<void *>(input), bytes, output, capacity, &count);
    *written = static_cast<ThreeMMI32>(count);
    return ok;
}
bool CodecManager::FGetCfmtFromBlck(PDataBlock pblck, LegacyLong *pcfmt)
{
    AssertThis(0);
    AssertPo(pblck, 0);
    AssertVarMem(pcfmt);
    TrashVar(pcfmt);
    const ThreeMMReadStream input = {pblck, static_cast<ThreeMMU32>(pblck->Cb(true)), ReadPackedHeader};
    ThreeMMI32 format;
    if (three_mm_packed_format(&input, &format) != THREE_MM_DATA_OK) return false;
    *pcfmt = format;
    return true;
}

void *CodecManager::_FindEncoder(void *context, ThreeMMI32 format)
{
    PCodec codec = nullptr;
    if (format == cfmtNil) return nullptr;
    return static_cast<CodecManager *>(context)->_FFindCodec(true, format, &codec) ? codec : nullptr;
}
int CodecManager::_ConvertEncoder(void *codec, ThreeMMI32 format, const void *input, ThreeMMU32 bytes,
                                  void *output, ThreeMMU32 capacity, ThreeMMI32 *written)
{
    LegacyLong count = *written;
    const bool ok = static_cast<PCodec>(codec)->FConvert(true, format, const_cast<void *>(input), bytes, output, capacity, &count);
    *written = static_cast<ThreeMMI32>(count);
    return ok;
}

/***************************************************************************
    Look for a codec that can handle the given format.
***************************************************************************/
bool CodecManager::_FFindCodec(bool fEncode, LegacyLong cfmt, PCodec *ppcodc)
{
    AssertThis(0);
    AssertVarMem(ppcodc);
    Assert(cfmtNil != cfmt, "nil cfmt");

    if (pvNil != _pcodcDef && _pcodcDef->FCanDo(fEncode, cfmt))
    {
        *ppcodc = _pcodcDef;
        return fTrue;
    }

    if (pvNil != _pglpcodc)
    {
        LegacyLong ipcodc;
        PCodec pcodc;

        for (ipcodc = _pglpcodc->IvMac(); ipcodc-- > 0;)
        {
            _pglpcodc->Get(ipcodc, &pcodc);
            if (pcodc->FCanDo(fEncode, cfmt))
            {
                *ppcodc = pcodc;
                return fTrue;
            }
        }
    }

    return fFalse;
}

/***************************************************************************
    Compress or decompress an hq of data. Note that the value of *phq
    may change. cfmt should be cfmtNil to decompress.
***************************************************************************/
int CodecManager::_CodeHandleBytes(void *context, const void *input, ThreeMMU32 bytes, void *output,
                                  ThreeMMU32 capacity, ThreeMMI32 *written)
{
    const auto call = static_cast<HandleCodeContext *>(context);
    LegacyLong count = *written;
    const bool ok = call->manager->_FCode(call->format, const_cast<void *>(input), bytes, output, capacity, &count);
    *written = count;
    return ok;
}
bool CodecManager::_FCodePhq(LegacyLong cfmt, HQ *phq)
{
    AssertThis(0);
    AssertVarMem(phq);
    AssertHq(*phq);

    HandleCodeContext context = {this, cfmt};
    int shrinkFailed = 0;
    const int result = three_mm_handle_code(KauaiHandleServices(), phq, &context, _CodeHandleBytes, &shrinkFailed);
    Assert(!shrinkFailed, "HQ shrink failed");
    if (result == THREE_MM_DATA_WRONG_SIZE) Bug("why is the final size larger?");
    return result == THREE_MM_DATA_OK;
}

/***************************************************************************
    Compress or decompress a block of data. If pvDst is nil, just fill
    *pcbDst with the required destination buffer size. This is just an
    estimate in the compress case.
***************************************************************************/
bool CodecManager::_FCode(LegacyLong cfmt, void *pvSrc, LegacyLong cbSrc, void *pvDst, LegacyLong cbDst, LegacyLong *pcbDst)
{
    AssertIn(cbSrc, 1, kcbMax);
    AssertPvCb(pvSrc, cbSrc);
    AssertIn(cbDst, 0, kcbMax);
    AssertPvCb(pvDst, cbDst);
    AssertVarMem(pcbDst);

    ThreeMMI32 count = 0;
    const ThreeMMPackedDecoder encoder = {this, _FindEncoder, _ConvertEncoder};
    const ThreeMMPackedDecoder decoder = {this, _FindDecoder, _ConvertDecoder};
    const int result = cfmt != cfmtNil ?
        three_mm_packed_encode(&encoder, cfmt, pvSrc, cbSrc, pvDst, cbDst, &count) :
        three_mm_packed_decode(&decoder, pvSrc, cbSrc, pvDst, cbDst, &count);
    if (result != THREE_MM_DATA_ARGUMENT && result != THREE_MM_DATA_NOT_ENCODED) *pcbDst = count;
    if (result == THREE_MM_DATA_WRONG_SIZE) Bug("codec produced wrong size");
    return result == THREE_MM_DATA_OK;
}
