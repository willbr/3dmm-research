/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Compression/decompression manager and default Kauai codec.

***************************************************************************/
#ifndef CODEC_H
#define CODEC_H
#include "three_mm/data_stream.h"
#include "three_mm/codec.h"
#include "three_mm/codec_registry.h"

/***************************************************************************
    Codec object.
***************************************************************************/
typedef class Codec *PCodec;
#define Codec_PAR BASE
#define kclsCodec 'CODC'
class Codec : public Codec_PAR
{
    RTCLASS_DEC

  public:
    // return whether this codec can handle the given format.
    virtual bool FCanDo(bool fEncode, LegacyLong cfmt) = 0;

    // Decompression should be extremely fast. Compression may be
    // (painfully) slow.
    virtual bool FConvert(bool fEncode, LegacyLong cfmt, void *pvSrc, LegacyLong cbSrc, void *pvDst, LegacyLong cbDst, LegacyLong *pcbDst) = 0;
};

/***************************************************************************
    Codec manager.
***************************************************************************/
typedef class CodecManager *PCodecManager;
#define CodecManager_PAR BASE
#define kclsCodecManager 'CODM'
class CodecManager : public CodecManager_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    static void *_FindDecoder(void *, ThreeMMI32);
    static int _ConvertDecoder(void *, ThreeMMI32, const void *, ThreeMMU32, void *, ThreeMMU32, ThreeMMI32 *);
    static void *_FindEncoder(void *, ThreeMMI32);
    static int _ConvertEncoder(void *, ThreeMMI32, const void *, ThreeMMU32, void *, ThreeMMU32, ThreeMMI32 *);
    struct HandleCodeContext { CodecManager *manager; LegacyLong format; };
    static int _CodeHandleBytes(void *, const void *, ThreeMMU32, void *, ThreeMMU32, ThreeMMI32 *);
    ThreeMMCodecRegistry _registry;

    virtual bool _FFindCodec(bool fEncode, LegacyLong cfmt, PCodec *ppcodc);
    virtual bool _FCodePhq(LegacyLong cfmt, HQ *phq);
    virtual bool _FCode(LegacyLong cfmt, void *pvSrc, LegacyLong cbSrc, void *pvDst, LegacyLong cbDst, LegacyLong *pcbDst);

  public:
    CodecManager(PCodec pcodc, LegacyLong cfmt);
    ~CodecManager(void);

    LegacyLong CfmtDefault(void)
    {
        return _registry.default_format;
    }
    void SetCfmtDefault(LegacyLong cfmt);
    virtual bool FRegisterCodec(PCodec pcodc);
    virtual bool FCanDo(LegacyLong cfmt, bool fEncode);

    // Gets the type of compression used on the block (assuming it is
    // compressed).
    virtual bool FGetCfmtFromBlck(PDataBlock pblck, LegacyLong *pcfmt);

    // FDecompress allows pvDst to be nil (in which case *pcbDst is filled
    // in with the buffer size required).
    // FCompress also allows pvDst to be nil, but the value returned in
    // *pcbDst will just be cbSrc - 1.
    bool FDecompressPhq(HQ *phq)
    {
        return _FCodePhq(cfmtNil, phq);
    }
    bool FCompressPhq(HQ *phq, LegacyLong cfmt = cfmtNil)
    {
        return _FCodePhq(cfmtNil == cfmt ? _registry.default_format : cfmt, phq);
    }

    bool FDecompress(void *pvSrc, LegacyLong cbSrc, void *pvDst, LegacyLong cbDst, LegacyLong *pcbDst)
    {
        return _FCode(cfmtNil, pvSrc, cbSrc, pvDst, cbDst, pcbDst);
    }
    bool FCompress(void *pvSrc, LegacyLong cbSrc, void *pvDst, LegacyLong cbDst, LegacyLong *pcbDst, LegacyLong cfmt = cfmtNil)
    {
        return _FCode(cfmtNil == cfmt ? _registry.default_format : cfmt, pvSrc, cbSrc, pvDst, cbDst, pcbDst);
    }
};

/***************************************************************************
    The standard Kauai Codec object.
***************************************************************************/
typedef class KauaiCodec *PKauaiCodec;
#define KauaiCodec_PAR Codec
#define kclsKauaiCodec 'KCDC'
class KauaiCodec : public KauaiCodec_PAR
{
    RTCLASS_DEC

  public:
    virtual bool FCanDo(bool fEncode, LegacyLong cfmt)
    {
        return kcfmtKauai2 == cfmt || kcfmtKauai == cfmt;
    }
    virtual bool FConvert(bool fEncode, LegacyLong cfmt, void *pvSrc, LegacyLong cbSrc, void *pvDst, LegacyLong cbDst, LegacyLong *pcbDst);
};

#endif //! CODEC_H
