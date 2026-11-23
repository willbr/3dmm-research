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
namespace {
const MMCodecServices *CodecServices()
{
    static const MMCodecServices services = {
        nullptr,
        [](void *, void *codec) { static_cast<PCodec>(codec)->AddRef(); },
        [](void *, void *codec) { auto native = static_cast<PCodec>(codec); ReleasePpo(&native); },
        [](void *, void *codec, int encode, MMI32 format) -> int {
            return static_cast<PCodec>(codec)->FCanDo(encode != 0, format);
        },
        [](void *, void *codec, int encode, MMI32 format, const void *input, MMU32 bytes,
           void *output, MMU32 capacity, MMI32 *written) -> int {
            LegacyLong count = *written;
            const bool ok = static_cast<PCodec>(codec)->FConvert(encode != 0, format, const_cast<void *>(input),
                                                                bytes, output, capacity, &count);
            *written = static_cast<MMI32>(count);
            return ok;
        }};
    return &services;
}
}
CodecManager::CodecManager(LegacyLong cfmt)
{
    AssertDo(mm_codec_registry_init_builtin(&_registry, CodecServices(), KauaiCopyAllocator(), cfmt) == MM_STORAGE_OK,
             "invalid built-in codec registry");
    AssertThis(0);
}
CodecManager::CodecManager(PCodec pcodc, LegacyLong cfmt)
{
    AssertNilOrPo(pcodc, 0); Assert(cfmtNil != cfmt, "nil default compression format");
    AssertDo(mm_codec_registry_init(&_registry, CodecServices(), KauaiCopyAllocator(), pcodc, cfmt) == MM_STORAGE_OK,
        "invalid codec registry");
    AssertThis(0);
}
CodecManager::~CodecManager(void)
{
    AssertThis(0);
    AssertDo(mm_codec_registry_dispose(&_registry) == MM_STORAGE_OK, "invalid codec registry disposal");
}
#ifdef DEBUG
void CodecManager::AssertValid(LegacyUlong grf)
{
    CodecManager_PAR::AssertValid(0);
    Assert(_registry.default_format != cfmtNil, "nil default compression");
    AssertNilOrPo(static_cast<PCodec>(_registry.default_codec), 0);
    Assert(_registry.count <= _registry.storage.buffers[0].capacity / sizeof(void *), "bad codec count");
}
void CodecManager::MarkMem(void)
{
    AssertValid(0); CodecManager_PAR::MarkMem();
    MarkMemObj(static_cast<PCodec>(_registry.default_codec));
    for (MMU32 i = _registry.count; i-- > 0;)
        MarkMemObj(static_cast<PCodec>(mm_codec_registry_at(&_registry, i)));
    MarkPv(_registry.storage.buffers[0].data);
}
#endif
void CodecManager::SetCfmtDefault(LegacyLong cfmt)
{
    AssertThis(0);
    if (mm_codec_registry_default(&_registry, cfmt) != MM_STORAGE_OK) Bug("can't set default compression to nil");
}
bool CodecManager::FRegisterCodec(PCodec pcodc)
{
    AssertThis(0); AssertPo(pcodc, 0);
    return mm_codec_registry_add(&_registry, pcodc) == MM_STORAGE_OK;
}

/***************************************************************************
    Return whether we can encode or decode the given format.
***************************************************************************/
bool CodecManager::FCanDo(LegacyLong cfmt, bool fEncode)
{
    AssertThis(0);
    return mm_codec_registry_can_code(&_registry, fEncode, cfmt) != 0;
}

/***************************************************************************
    Gets the type of compression used on the block (assuming it is
    compressed).
***************************************************************************/
namespace {
int ReadPackedHeader(void *context, MMU32 offset, void *bytes, size_t count)
{
    if (offset > 0x7fffffffUL || count > 0x7fffffffUL) return 0;
    return static_cast<PDataBlock>(context)->FReadRgb(bytes, static_cast<LegacyLong>(count), offset, true);
}
} // namespace
bool CodecManager::FGetCfmtFromBlck(PDataBlock pblck, LegacyLong *pcfmt)
{
    AssertThis(0);
    AssertPo(pblck, 0);
    AssertVarMem(pcfmt);
    TrashVar(pcfmt);
    const MMReadStream input = {pblck, static_cast<MMU32>(pblck->Cb(true)), ReadPackedHeader};
    MMI32 format;
    if (mm_packed_format(&input, &format) != MM_DATA_OK) return false;
    *pcfmt = format;
    return true;
}

/***************************************************************************
    Compress or decompress an hq of data. Note that the value of *phq
    may change. cfmt should be cfmtNil to decompress.
***************************************************************************/
int CodecManager::_CodeHandleBytes(void *context, const void *input, MMU32 bytes, void *output,
                                  MMU32 capacity, MMI32 *written)
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
    const int result = mm_handle_code(KauaiHandleServices(), phq, &context, _CodeHandleBytes, &shrinkFailed);
    Assert(!shrinkFailed, "HQ shrink failed");
    if (result == MM_DATA_WRONG_SIZE) Bug("why is the final size larger?");
    return result == MM_DATA_OK;
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

    MMI32 count = 0;
    int builtinStatus;
    const int result = mm_codec_registry_code(&_registry, cfmt, pvSrc, cbSrc, pvDst, cbDst, &count, &builtinStatus);
    if (builtinStatus != MM_DATA_OK)
    {
        TrashVar(&count);
        if (cfmt == cfmtNil) Bug("bad compressed data");
        else if (builtinStatus == MM_DATA_MEMORY) Warn("failed to allocate memory for links");
    }
    if (result != MM_DATA_ARGUMENT && result != MM_DATA_NOT_ENCODED) *pcbDst = count;
    if (result == MM_DATA_WRONG_SIZE) Bug("codec produced wrong size");
    return result == MM_DATA_OK;
}
