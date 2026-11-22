/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Chunky file compiler and decompiler class implementations.

***************************************************************************/
#include "kidframe.h" //because we need scrcomg
#include "handle_services.h"
#include "mm/chunky_assembly.h"
#include "mm/chunky_collection.h"
#include <cstring>
ASSERTNAME

namespace Chunky {

RTCLASS(Compiler)
RTCLASS(CompilerLexer)
RTCLASS(Decompiler)

PZString _mpertpsz[] = {
    PszLit("no error"),
    PszLit("Internal allocation error"),                      // ertOom
    PszLit("Can't open the given file"),                      // ertOpenFile
    PszLit("Can't read the given metafile"),                  // ertReadMeta
    PszLit("Number not in range for BYTE"),                   // ertRangeByte
    PszLit("Number not in range for SHORT"),                  // ertRangeShort
    PszLit("Invalid data before atomic chunk"),               // ertBufData
    PszLit("Open parenthesis '(' expected"),                  // ertParenOpen
    PszLit("Unexpected end of file"),                         // ertEof
    PszLit("String expected"),                                // ertNeedString
    PszLit("Numeric value expected"),                         // ertNeedNumber
    PszLit("Unexpected Token"),                               // ertBadToken
    PszLit("Close parenthesis ')' expected"),                 // ertParenClose
    PszLit("Invalid CHUNK declaration"),                      // ertChunkHead
    PszLit("Duplicate CHUNK declaration"),                    // ertDupChunk
    PszLit("Invalid CHILD declaration"),                      // ertBodyChildHead
    PszLit("Child chunk doesn't exist"),                      // ertChildMissing
    PszLit("A cycle would be created by this adoption"),      // ertCycle
    PszLit("Invalid PARENT declaration"),                     // ertBodyParentHead
    PszLit("Parent chunk doesn't exist"),                     // ertParentMissing
    PszLit("Alignment parameter out of range"),               // ertBodyAlignRange
    PszLit("File name expected"),                             // ertBodyFile
    PszLit("ENDCHUNK expected"),                              // ertNeedEndChunk
    PszLit("Invalid DynamicArray or AllocatedArray declaration"),                   // ertListHead
    PszLit("Invalid size for list entries"),                  // ertListEntrySize
    PszLit("Variable undefined"),                             // ertVarUndefined
    PszLit("Too much data for item"),                         // ertItemOverflow
    PszLit("Can't have a free item in a general collection"), // ertBadFree
    PszLit("Syntax error"),                                   // ertSyntax
    PszLit("Invalid GeneralGroup or AllocatedGroup declaration"),                   // ertGroupHead
    PszLit("Invalid size for fixed group data"),              // ertGroupEntrySize
    PszLit("Invalid StringTable_GST or AllocatedStringTable declaration"),                 // ertGstHead
    PszLit("Invalid size for extra string table data"),       // ertGstEntrySize
    PszLit("Script compilation failed"),                      // ertScript
    PszLit("Invalid ADOPT declaration"),                      // ertAdoptHead
    PszLit("CHUNK declaration expected"),                     // ertNeedChunk
    PszLit("Invalid BITMAP declaration"),                     // ertBodyBitmapHead
    PszLit("Can't read the given bitmap file"),               // ertReadBitmap
    PszLit("Disassembling the script failed"),                // ertBadScript
    PszLit("Can't read the given cursor file"),               // ertReadCursor
    PszLit("Can't read given file as a packed file"),         // ertPackedFile
    PszLit("Can't read the given midi file"),                 // ertReadMidi
    PszLit("Bad pack format"),                                // ertBadPackFmt
    PszLit("Illegal LONER primitive in SUBFILE"),             // ertLonerInSub
    PszLit("Unterminated SUBFILE"),                           // ertNoEndSubFile
};

/***************************************************************************
    Constructor for the Compiler class.
***************************************************************************/
Compiler::Compiler(void)
{
    _pcfl = pvNil;
    _pchlx = pvNil;
    _pmsnkError = pvNil;
    _cactError = 0;
    AssertThis(0);
}

/***************************************************************************
    Destructor for the Compiler class.
***************************************************************************/
Compiler::~Compiler(void)
{
    ReleasePpo(&_pcfl);
    ReleasePpo(&_pchlx);
}

#ifdef DEBUG
/***************************************************************************
    Assert that the Compiler is a valid object.
***************************************************************************/
void Compiler::AssertValid(LegacyUlong grf)
{
    Compiler_PAR::AssertValid(grf);
    AssertNilOrPo(_pcfl, 0);
    AssertPo(&_bsf, 0);
    AssertNilOrPo(_pchlx, 0);
    AssertNilOrPo(_pmsnkError, 0);
}

/***************************************************************************
    Mark memory for the Compiler object.
***************************************************************************/
void Compiler::MarkMem(void)
{
    AssertThis(0);

    Compiler_PAR::MarkMem();
    MarkMemObj(_pcfl);
    MarkMemObj(&_bsf);
    MarkMemObj(_pchlx);
}
#endif // DEBUG

/***************************************************************************
    Registers an error, prints error message with filename and line number.
    pszMessage may be nil.
***************************************************************************/
void Compiler::_Error(LegacyLong ert, PZString pszMessage)
{
    AssertThis(0);
    AssertIn(ert, ertNil, ertLim);
    AssertPo(_pchlx, 0);
    String stnFile;
    String stn;

    _pchlx->GetStnFile(&stnFile);
    if (ertNil == ert)
    {
        stn.FFormatSz(pszMessage == pvNil ? PszLit("%s(%d:%d) : warning") : PszLit("%s(%d:%d) : warning : %z"),
                      &stnFile, _pchlx->LwLine(), _pchlx->IchLine(), pszMessage);
    }
    else
    {
        _cactError++;
        stn.FFormatSz(pszMessage == pvNil ? PszLit("%s(%d:%d) : error : %z") : PszLit("%s(%d:%d) : error : %z : %z"),
                      &stnFile, _pchlx->LwLine(), _pchlx->IchLine(), _mpertpsz[ert], pszMessage);
    }

    _pmsnkError->ReportLine(stn.Psz());
}

MMChunkySourceToken Compiler::_SourceToken(PToken ptok)
{
    const bool text = ptok->tt == ttString;
    return {static_cast<int>(ptok->tt), ptok->tt == ttLong ? static_cast<MMI32>(ptok->lw) : 0,
            text ? ptok->stn.Prgch() : nullptr, text ? static_cast<MMU32>(ptok->stn.Cch()) : 0};
}

MMChunkySource Compiler::_ChunkySource(SourceCursor *cursor)
{
    MMChunkySource source{};
    source.context = cursor;
    source.next = [](void *context, MMChunkySourceToken *out, int mode) -> int {
        auto input = static_cast<SourceCursor *>(context);
        const bool found = mode == MM_CHUNKY_READ_RAW ? input->compiler->_pchlx->FGetTok(input->token)
            : input->compiler->_FGetCleanTok(input->token, mode == MM_CHUNKY_READ_CLEAN_EOF);
        if (found)
            *out = _SourceToken(input->token);
        return found;
    };
    source.report = [](void *context, int error) {
        auto compiler = static_cast<SourceCursor *>(context)->compiler;
        if (error == ertBodyAlignRange)
        {
            String format;
            format.FFormatSz(PszLit("legal range for alignment is (%d, %d)"),
                             static_cast<LegacyLong>(MM_CHUNKY_ALIGN_MIN), static_cast<LegacyLong>(MM_CHUNKY_ALIGN_MAX));
            compiler->_Error(error, format.Psz());
        }
        else
            compiler->_Error(error);
    };
    source.append = [](void *context, const void *bytes, MMU32 size) -> int {
        auto compiler = static_cast<SourceCursor *>(context)->compiler;
        return compiler->FError() || compiler->_bsf.FReplace(const_cast<void *>(bytes), size, compiler->_bsf.IbMac(), 0);
    };
    source.size = [](void *context) -> MMU32 {
        return static_cast<SourceCursor *>(context)->compiler->_bsf.IbMac();
    };
    source.import_file = [](void *context) {
        static_cast<SourceCursor *>(context)->compiler->_ParseBodyFile();
    };
    source.format = &_format;
    source.unit_bytes = sizeof(achar);
    source.native_encoding = koskCur;
    source.native_order = kboCur;
    return source;
}

/***************************************************************************
    Get a token, automatically handling mode change commands and negatives.
    Return true iff *ptok is valid, not whether an error occurred.
***************************************************************************/
bool Compiler::_FGetCleanTok(Token *ptok, bool fEofOk)
{
    AssertThis(0);
    AssertVarMem(ptok);

    static const MMChunkyFormats formats{MacWin(kboCur, kboOther), MacWin(kboOther, kboCur), koskMac, koskWin};
    int sign = 0;
    while (_pchlx->FGetTokSkipSemi(ptok))
    {
        MMI32 value = ptok->tt == ttLong ? ptok->lw : 0;
        if (!mm_chunky_clean_token(&_format, &formats, ptok->tt, &value, &sign))
            continue;
        if (ptok->tt == ttLong)
            ptok->lw = value;
        if (sign)
            _Error(ertSyntax);
        return fTrue;
    }
    if (sign)
        _Error(ertSyntax);
    if (!fEofOk)
        _Error(ertEof);
    return fFalse;
}

/***************************************************************************
    Skip tokens until we encounter the given token type.
***************************************************************************/
void Compiler::_SkipPastTok(LegacyLong tt)
{
    AssertThis(0);
    Token tok;

    while (_FGetCleanTok(&tok) && tt != tok.tt)
        ;
}

/***************************************************************************
    Parse a parenthesized header from the source file.
***************************************************************************/
bool Compiler::_FParseParenHeader(PHP *prgphp, LegacyLong cphpMax, LegacyLong *pcphp)
{
    AssertThis(0);
    AssertIn(cphpMax, 1, kcbMax);
    AssertPvCb(prgphp, LwMul(cphpMax, size(PHP)));
    AssertVarMem(pcphp);

    Token token;
    SourceCursor cursor{this, &token};
    const auto source = _ChunkySource(&cursor);
    const MMI32 count = mm_chunky_parse_header(&source, prgphp, cphpMax);
    if (count < 0)
    {
        TrashVar(pcphp);
        return fFalse;
    }
    *pcphp = count;
    return fTrue;
}

/***************************************************************************
    Parse a file statement from the source file.
***************************************************************************/
void Compiler::_ParseBodyFile(void)
{
    AssertThis(0);
    Filename fni;
    FileLocation floSrc;

    if (!_pchlx->FGetPath(&fni))
    {
        _Error(ertBodyFile);
        _SkipPastTok(ttEndChunk);
        return;
    }

    if (pvNil == (floSrc.pfil = FileObject::PfilOpen(&fni)))
    {
        _Error(ertOpenFile);
        return;
    }

    floSrc.fp = 0;
    floSrc.cb = floSrc.pfil->FpMac();
    if (!_bsf.FReplaceFlo(&floSrc, fFalse, _bsf.IbMac(), 0))
        _Error(ertOom);

    ReleasePpo(&floSrc.pfil);
}

/***************************************************************************
    Start a write operation. If fPack is true, allocate a temporary block.
    Otherwise, get the block on the ChunkyFile. The caller should write its data
    into the pblck, then call _FEndWrite to complete the operation.
***************************************************************************/
bool Compiler::_FPrepWrite(bool fPack, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck)
{
    AssertThis(0);
    AssertPo(pblck, 0);

    if (fPack)
    {
        pblck->Free();
        Assert(!pblck->FPacked(), "why is block packed?");
        return pblck->FSetTemp(cb);
    }

    return _pcfl->FPut(cb, ctg, cno, pblck);
}

/***************************************************************************
    Balances a call to _FPrepWrite.
***************************************************************************/
bool Compiler::_FEndWrite(bool fPack, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck)
{
    AssertThis(0);
    AssertPo(pblck, fblckUnpacked);

    if (fPack)
    {
        // we don't fail if we can't compress it
        pblck->FPackData();
        return _pcfl->FPutBlck(pblck, ctg, cno);
    }

    AssertPo(pblck, fblckFile);
    return fTrue;
}

/***************************************************************************
    Parse a metafile import command from the source file.
***************************************************************************/
void Compiler::_ParseBodyMeta(bool fPack, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    Filename fni;
    DataBlock blck;
    PPicture ppic;
    Token tok;

    if (!_pchlx->FGetPath(&fni))
    {
        _Error(ertBodyFile);
        _SkipPastTok(ttEndChunk);
        return;
    }

    if (pvNil == (ppic = Picture::PpicReadNative(&fni)))
    {
        _Error(ertReadMeta);
        return;
    }

    if (!FError())
    {
        if (!_FPrepWrite(fPack, ppic->CbOnFile(), ctg, cno, &blck) || !ppic->FWrite(&blck) ||
            !_FEndWrite(fPack, ctg, cno, &blck))
        {
            _Error(ertOom);
        }
    }
    ReleasePpo(&ppic);

    if (_FGetCleanTok(&tok) && ttEndChunk != tok.tt)
    {
        _Error(ertNeedEndChunk);
        _SkipPastTok(ttEndChunk);
    }
}

/***************************************************************************
    Parse a bitmap import command from the source file.
***************************************************************************/
void Compiler::_ParseBodyBitmap(bool fPack, bool fMask, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    Filename fni;
    DataBlock blck;
    Token tok;
    PHP rgphp[3];
    LegacyLong cphp;
    PMaskedBitmapMBMP pmbmp = pvNil;

    ClearPb(rgphp, size(rgphp));
    if (!_FParseParenHeader(rgphp, 3, &cphp))
    {
        _Error(ertBodyBitmapHead);
        return;
    }

    if (!_pchlx->FGetPath(&fni))
    {
        _Error(ertBodyFile);
        goto LFail;
    }

    if (pvNil == (pmbmp = ImportMaskedBitmap(&fni, (byte)rgphp[0].number, rgphp[1].number, rgphp[2].number,
                                                fMask ? fmbmpMask : fmbmpNil)))
    {
        String stn;
        fni.GetStnPath(&stn);
        _Error(ertReadBitmap, stn.Psz());
        goto LFail;
    }

    if (!FError())
    {
        if (!_FPrepWrite(fPack, MaskedBitmapSize(pmbmp), ctg, cno, &blck) || !WriteMaskedBitmap(pmbmp, &blck) ||
            !_FEndWrite(fPack, ctg, cno, &blck))
        {
            _Error(ertOom);
        }
    }
    ReleaseMaskedBitmap(&pmbmp);

    if (_FGetCleanTok(&tok) && ttEndChunk != tok.tt)
    {
        _Error(ertNeedEndChunk);
    LFail:
        _SkipPastTok(ttEndChunk);
    }
}

/***************************************************************************
    Parse a palette import command from the source file.
***************************************************************************/
void Compiler::_ParseBodyPalette(bool fPack, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    Filename fni;
    DataBlock blck;
    Token tok;
    PDynamicArray pglclr;

    if (!_pchlx->FGetPath(&fni))
    {
        _Error(ertBodyFile);
        goto LFail;
    }

    if (!FReadBitmap(&fni, pvNil, &pglclr, pvNil, pvNil, pvNil))
    {
        _Error(ertReadBitmap);
        goto LFail;
    }

    if (!FError())
    {
        if (!_FPrepWrite(fPack, pglclr->CbOnFile(), ctg, cno, &blck) || !pglclr->FWrite(&blck) ||
            !_FEndWrite(fPack, ctg, cno, &blck))
        {
            _Error(ertOom);
        }
    }
    ReleasePpo(&pglclr);

    if (_FGetCleanTok(&tok) && ttEndChunk != tok.tt)
    {
        _Error(ertNeedEndChunk);
    LFail:
        _SkipPastTok(ttEndChunk);
    }
}

/***************************************************************************
    Parse a midi import command from the source file.
***************************************************************************/
void Compiler::_ParseBodyMidi(bool fPack, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    Filename fni;
    DataBlock blck;
    Token tok;
    PMidiStream pmids;

    if (!_pchlx->FGetPath(&fni))
    {
        _Error(ertBodyFile);
        goto LFail;
    }

    if (pvNil == (pmids = MidiStream::PmidsReadNative(&fni)))
    {
        _Error(ertReadMidi);
        goto LFail;
    }

    if (!FError())
    {
        if (!_FPrepWrite(fPack, pmids->CbOnFile(), ctg, cno, &blck) || !pmids->FWrite(&blck) ||
            !_FEndWrite(fPack, ctg, cno, &blck))
        {
            _Error(ertOom);
        }
    }
    ReleasePpo(&pmids);

    if (_FGetCleanTok(&tok) && ttEndChunk != tok.tt)
    {
        _Error(ertNeedEndChunk);
    LFail:
        _SkipPastTok(ttEndChunk);
    }
}

/***************************************************************************
    Parse a cursor import command from the source file.
***************************************************************************/
void Compiler::_ParseBodyCursor(bool fPack, ChunkTagOrType ctg, ChunkNumber cno)
{
    // These are for parsing a Windows cursor file
    struct CURDIR
    {
        byte dxp;
        byte dyp;
        byte bZero1;
        byte bZero2;
        short xp;
        short yp;
        LegacyLong cb;
        LegacyLong bv;
    };

    struct CURH
    {
        LegacyLong cbCurh;
        LegacyLong dxp;
        LegacyLong dyp;
        short swOne1;
        short swOne2;
        LegacyLong lwZero1;
        LegacyLong lwZero2;
        LegacyLong lwZero3;
        LegacyLong lwZero4;
        LegacyLong lwZero5;
        LegacyLong lwZero6;
        LegacyLong lw1;
        LegacyLong lw2;
    };

    AssertThis(0);
    Filename fni;
    DataBlock blck;
    FileLocation floSrc;
    Token tok;
    LegacyLong ccurdir, cbBits;
    CursorOnFile curf;
    short rgsw[3];
    byte *prgb;
    CURDIR *pcurdir;
    CURH *pcurh;
    PGeneralGroup pggcurf = pvNil;
    HQ hq = hqNil;

    floSrc.pfil = pvNil;
    if (!_pchlx->FGetPath(&fni))
    {
        _Error(ertBodyFile);
        _SkipPastTok(ttEndChunk);
        return;
    }

    if (pvNil == (floSrc.pfil = FileObject::PfilOpen(&fni)))
    {
        _Error(ertReadCursor);
        goto LFail;
    }

    floSrc.fp = 0;
    floSrc.cb = floSrc.pfil->FpMac();
    if (floSrc.cb < size(rgsw) + size(CURDIR) + size(CURH) + 128 || !floSrc.FReadRgb(rgsw, size(rgsw), 0) ||
        rgsw[0] != 0 || rgsw[1] != 2 || !FIn(ccurdir = rgsw[2], 1, (floSrc.cb - size(rgsw)) / size(CURDIR)))
    {
        _Error(ertReadCursor);
        goto LFail;
    }

    floSrc.cb -= size(rgsw);
    floSrc.fp = size(rgsw);
    if (!floSrc.FReadHq(&hq))
    {
        _Error(ertOom);
        goto LFail;
    }
    ReleasePpo(&floSrc.pfil);

    prgb = (byte *)PvLockHq(hq);
    pcurdir = (CURDIR *)prgb;
    if (pvNil == (pggcurf = GeneralGroup::PggNew(size(CursorOnFile), ccurdir)))
    {
        _Error(ertOom);
        goto LFail;
    }
    while (ccurdir-- > 0)
    {
        cbBits = pcurdir->dxp == 32 ? 256 : 128;
        if (pcurdir->dxp != pcurdir->dyp || pcurdir->dxp != 16 && pcurdir->dxp != 32 || pcurdir->bZero1 != 0 ||
            pcurdir->bZero2 != 0 || pcurdir->cb != size(CURH) + cbBits ||
            !FIn(pcurdir->bv -= size(rgsw), LwMul(rgsw[2], size(CURDIR)), floSrc.cb - pcurdir->cb + 1) ||
            CbRoundToLong(pcurdir->bv) != pcurdir->bv)
        {
            _Error(ertReadCursor);
            goto LFail;
        }
        curf.curt = curtMonochrome;
        curf.xp = (byte)pcurdir->xp;
        curf.yp = (byte)pcurdir->yp;
        curf.dxp = pcurdir->dxp;
        curf.dyp = pcurdir->dyp;

        pcurh = (CURH *)PvAddBv(prgb, pcurdir->bv);
        if (pcurh->cbCurh != size(CURH) - 2 * size(LegacyLong) || pcurh->dxp != pcurdir->dxp ||
            pcurh->dyp != 2 * pcurdir->dyp || pcurh->swOne1 != 1 || pcurh->swOne2 != 1 || pcurh->lwZero1 != 0 ||
            (pcurh->lwZero2 != 0 && pcurh->lwZero2 != pcurdir->cb - size(CURH)) || pcurh->lwZero3 != 0 ||
            pcurh->lwZero4 != 0 || pcurh->lwZero5 != 0 || pcurh->lwZero6 != 0)
        {
            _Error(ertReadCursor);
            goto LFail;
        }

        // The bits are stored in upside down DIB order!
        ReversePb(pcurh + 1, pcurdir->cb - size(CURH));
        SwapBytesRglw(pcurh + 1, (pcurdir->cb - size(CURH)) / size(int32_t));

        if (pcurdir->dxp == 16)
        {
            // need to consolidate the bits, because they are stored 4 bytes per
            // row (2 bytes wasted) instead of 2 bytes per row.
            LegacyLong csw = 32;
            short *pswSrc, *pswDst;

            pswSrc = pswDst = (short *)(pcurh + 1);
            while (csw-- != 0)
            {
                *pswDst++ = *pswSrc++;
                pswSrc++;
            }
        }

        if (!pggcurf->FInsert(pggcurf->IvMac(), (LegacyLong)curf.dxp * curf.dyp / 4, pcurh + 1, &curf))
        {
            _Error(ertOom);
            goto LFail;
        }
        pcurdir++;
    }

LFail:
    // success comes through here also
    ReleasePpo(&floSrc.pfil);
    if (hqNil != hq)
    {
        UnlockHq(hq);
        FreePhq(&hq);
    }

    if (!FError())
    {
        if (!_FPrepWrite(fPack, pggcurf->CbOnFile(), ctg, cno, &blck) || !pggcurf->FWrite(&blck) ||
            !_FEndWrite(fPack, ctg, cno, &blck))
        {
            _Error(ertOom);
        }
    }
    ReleasePpo(&pggcurf);

    if (_FGetCleanTok(&tok) && ttEndChunk != tok.tt)
    {
        _Error(ertNeedEndChunk);
        _SkipPastTok(ttEndChunk);
    }
}

void Compiler::_ParseBodyCollection(bool fPack, int kind, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    struct Target
    {
        Compiler *compiler;
        bool pack;
        ChunkTagOrType tag;
        ChunkNumber number;
    } target{this, fPack, ctg, cno};

    Token token;
    SourceCursor cursor{this, &token};
    const auto source = _ChunkySource(&cursor);
    const auto allocator = Group::CollectionAllocator();
    MMChunkyCollectionHost host{};
    host.context = &target;
    host.allocator = &allocator;
#ifdef DEBUG
    host.trash_unused = 1;
#endif
    host.has_error = [](void *context) -> int {
        return static_cast<Target *>(context)->compiler->FError();
    };
    host.clear_payload = [](void *context) {
        auto &buffer = static_cast<Target *>(context)->compiler->_bsf;
        buffer.FReplace(pvNil, 0, 0, buffer.IbMac());
    };
    host.read_payload = [](void *context, void *output, MMU32 bytes) {
        static_cast<Target *>(context)->compiler->_bsf.FetchRgb(0, bytes, output);
    };
    host.save = [](void *context, MMU32 bytes, MMChunkyCollectionWrite write, void *collection) -> int {
        auto out = static_cast<Target *>(context);
        DataBlock block;
        if (!out->compiler->_FPrepWrite(out->pack, bytes, out->tag, out->number, &block))
            return 0;
        const MMStorageWriter writer{&block, bytes,
            [](void *target, MMU32 offset, const void *data, size_t count) -> int {
                return static_cast<PDataBlock>(target)->FWriteRgb(const_cast<void *>(data), count, offset);
            }};
        return write(collection, &writer) && out->compiler->_FEndWrite(out->pack, out->tag, out->number, &block);
    };
    mm_chunky_parse_collection(&source, &host, kind);
}

/***************************************************************************
    Parse a script from the source file.
***************************************************************************/
void Compiler::_ParseBodyScript(bool fPack, bool fInfix, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    GraphicsObjectCompiler sccg;
    PScript pscpt;

    if (pvNil == (pscpt = sccg.PscptCompileLex(_pchlx, fInfix, _pmsnkError, ttEndChunk)))
    {
        _Error(ertScript);
        return;
    }

    if (!ScriptInterpreter::SaveScript(pscpt, _pcfl, ctg, cno, fPack))
        _Error(ertOom);

    ScriptInterpreter::ReleaseScript(&pscpt);
}

bool Compiler::_FWriteSubfile(PChunkyFile parent, bool fPack, ChunkTagOrType ctg, ChunkNumber cno)
{
    struct Transfer
    {
        PChunkyFile source;
        PFileObject destination;
    } transfer{_pcfl, pvNil};
    const MMChunkyForestHost forest{&transfer,
        [](void *context, MMU32 index, MMChunkRecordKey *key, MMU32 *references) -> int {
            auto file = static_cast<Transfer *>(context)->source;
            ChunkIdentification cki;
            if (!file->FGetCki(index, &cki))
                return 0;
            *key = {cki.ctg, cki.cno};
            *references = file->CckiRef(cki.ctg, cki.cno);
            return 1;
        },
        [](void *context, MMChunkRecordKey key, MMU32 offset, MMU32 *bytes) -> int {
            auto move = static_cast<Transfer *>(context);
            LegacyLong written;
            if (!move->source->FWriteChunkTree(key.ctg, key.cno, move->destination, offset, &written))
                return 0;
            *bytes = written;
            return 1;
        }
    };

    MMU32 extent, written;
    if (!mm_chunky_write_roots(&forest, 0, &extent))
        return false;

    FilePosition start = 0;
    if (fPack)
    {
        transfer.destination = FileObject::PfilCreateTemp();
        if (!transfer.destination)
            return false;
    }
    else
    {
        FileLocation output;
        if (!parent->FPut(extent, ctg, cno) || !parent->FFindFlo(ctg, cno, &output))
            return false;
        transfer.destination = output.pfil;
        transfer.destination->AddRef();
        start = output.fp;
    }

    bool ok = mm_chunky_write_roots(&forest, start, &written) && written == extent;
    if (ok && fPack)
    {
        DataBlock block(transfer.destination, 0, written);
        block.FPackData();
        ok = parent->FPutBlck(&block, ctg, cno);
    }
    if (ok)
        parent->SetForest(ctg, cno, fTrue);
    ReleasePpo(&transfer.destination);
    return ok;
}

bool Compiler::_FFinishChunkData(bool fPack, bool fPrePacked, ChunkTagOrType ctg, ChunkNumber cno)
{
    DataBlock block;
    if (!_FPrepWrite(fPack, _bsf.IbMac(), ctg, cno, &block) || !_bsf.FWriteRgb(&block) ||
        !_FEndWrite(fPack, ctg, cno, &block))
        return false;
    if (fPrePacked)
        _pcfl->SetPacked(ctg, cno, fTrue);
    return true;
}

void Compiler::_ParseSpecialBody(int kind, bool fPack, ChunkTagOrType ctg, ChunkNumber cno)
{
    switch (kind)
    {
    case ttMeta: _ParseBodyMeta(fPack, ctg, cno); break;
    case ttBitmap:
    case ttMask: _ParseBodyBitmap(fPack, kind == ttMask, ctg, cno); break;
    case ttPalette: _ParseBodyPalette(fPack, ctg, cno); break;
    case ttMidi: _ParseBodyMidi(fPack, ctg, cno); break;
    case ttCursor: _ParseBodyCursor(fPack, ctg, cno); break;
    case ttAl:
    case ttGl:
    case ttAg:
    case ttGg:
    case ttAst:
    case ttGst: _ParseBodyCollection(fPack, kind, ctg, cno); break;
    case ttScript:
    case ttScriptP: _ParseBodyScript(fPack, kind == ttScript, ctg, cno); break;
    default: Bug("bad special chunk body"); break;
    }
}

void Compiler::_Assemble(void)
{
    Token token;
    SourceCursor cursor{this, &token};
    const auto source = _ChunkySource(&cursor);
    const auto allocator = Group::CollectionAllocator();
    MMChunkyAssemblyHost host{};
    host.context = this;
    host.allocator = &allocator;
    host.error_count = [](void *context) -> MMU32 {
        return static_cast<Compiler *>(context)->_cactError;
    };
    host.error_detail = [](void *context, int error, const char *ascii) {
        String detail;
        detail.SetSzs(reinterpret_cast<PSZS>(const_cast<char *>(ascii)));
        static_cast<Compiler *>(context)->_Error(error, detail.Psz());
    };
    host.error_limit = [](void *context) {
        static_cast<Compiler *>(context)->_pmsnkError->ReportLine(PszLit("Too many errors - compilation aborted"));
    };
    host.exists = [](void *context, MMChunkRecordKey key) -> int {
        return static_cast<Compiler *>(context)->_pcfl->FFind(key.ctg, key.cno);
    };
    host.create = [](void *context, MMChunkRecordKey key) -> int {
        return static_cast<Compiler *>(context)->_pcfl->FPutPv(pvNil, 0, key.ctg, key.cno);
    };
    host.set_name = [](void *context, MMChunkRecordKey key, const MMLexText *text) -> int {
        auto compiler = static_cast<Compiler *>(context);
        achar chars[MM_LEX_TEXT_CAPACITY];
        std::memcpy(chars, &text->units, text->length * sizeof(achar));
        String name;
        name.SetRgch(chars, text->length);
        return compiler->_pcfl->FSetName(key.ctg, key.cno, &name);
    };
    host.reaches = [](void *context, MMChunkRecordKey child, MMChunkRecordKey parent) -> int {
        return static_cast<Compiler *>(context)->_pcfl->TIsDescendent(child.ctg, child.cno, parent.ctg, parent.cno) != tNo;
    };
    host.adopt = [](void *context, MMChunkRecordKey parent, MMChunkRecordKey child, MMU32 slot) -> int {
        return static_cast<Compiler *>(context)->_pcfl->FAdoptChild(parent.ctg, parent.cno, child.ctg, child.cno, slot, fTrue);
    };
    host.set_loner = [](void *context, MMChunkRecordKey key) {
        static_cast<Compiler *>(context)->_pcfl->SetLoner(key.ctg, key.cno, fTrue);
    };
    host.clear_data = [](void *context) {
        auto &buffer = static_cast<Compiler *>(context)->_bsf;
        buffer.FReplace(pvNil, 0, 0, buffer.IbMac());
    };
    host.read_prefix = [](void *context, void *bytes, MMU32 count) {
        static_cast<Compiler *>(context)->_bsf.FetchRgb(0, count, bytes);
    };
    host.remove_prefix = [](void *context, MMU32 count) {
        static_cast<Compiler *>(context)->_bsf.FReplace(pvNil, 0, 0, count);
    };
    host.pack_format = [](void *, MMI32 format) -> int {
        if (!vpcodmUtil->FCanDo(format, fTrue))
            return 0;
        vpcodmUtil->SetCfmtDefault(format);
        return 1;
    };
    host.finish_data = [](void *context, MMChunkRecordKey key, int pack, int prepacked) -> int {
        return static_cast<Compiler *>(context)->_FFinishChunkData(pack != 0, prepacked != 0, key.ctg, key.cno);
    };
    host.special_body = [](void *context, MMChunkRecordKey key, int kind, int pack) {
        static_cast<Compiler *>(context)->_ParseSpecialBody(kind, pack != 0, key.ctg, key.cno);
    };
    host.finish_archive = [](void *context) -> int {
        return static_cast<Compiler *>(context)->_pcfl->FSave(kctgChkCmp, pvNil);
    };
    host.create_subfile = [](void *) -> void * {
        return ChunkyFile::PcflCreateTemp();
    };
    host.activate = [](void *context, void *file) {
        static_cast<Compiler *>(context)->_pcfl = static_cast<PChunkyFile>(file);
    };
    host.release = [](void *, void *file) {
        static_cast<PChunkyFile>(file)->Release();
    };
    host.finish_subfile = [](void *context, void *parent, MMChunkRecordKey key, int pack) -> int {
        return static_cast<Compiler *>(context)->_FWriteSubfile(static_cast<PChunkyFile>(parent), pack != 0, key.ctg, key.cno);
    };
    mm_chunky_assemble(&source, &host, _pcfl);
}

/***************************************************************************
    Compile the given file.
***************************************************************************/
PChunkyFile Compiler::PcflCompile(PFilename pfniSrc, PFilename pfniDst, PMSNK pmsnk)
{
    AssertThis(0);
    AssertPo(pfniSrc, ffniFile);
    AssertPo(pfniDst, ffniFile);
    AssertPo(pmsnk, 0);
    FileByteStream bsfSrc;
    String stnFile;
    FileLocation flo;
    bool fRet;

    if (pvNil == (flo.pfil = FileObject::PfilOpen(pfniSrc)))
    {
        pmsnk->ReportLine(PszLit("opening source file failed"));
        return pvNil;
    }

    flo.fp = 0;
    flo.cb = flo.pfil->FpMac();
    fRet = flo.FTranslate(oskNil) && bsfSrc.FReplaceFlo(&flo, fFalse, 0, 0);
    ReleasePpo(&flo.pfil);
    if (!fRet)
        return pvNil;

    pfniSrc->GetStnPath(&stnFile);
    return PcflCompile(&bsfSrc, &stnFile, pfniDst, pmsnk);
}

/***************************************************************************
    Compile the given FileByteStream, using initial file name given by pstnFile.
***************************************************************************/
PChunkyFile Compiler::PcflCompile(PFileByteStream pbsfSrc, PString pstnFile, PFilename pfniDst, PMSNK pmsnk)
{
    AssertThis(0);
    AssertPo(pbsfSrc, ffniFile);
    AssertPo(pstnFile, 0);
    AssertPo(pfniDst, ffniFile);
    AssertPo(pmsnk, 0);
    PChunkyFile pcfl;

    if (pvNil == (_pchlx = NewObj CompilerLexer(pbsfSrc, pstnFile)))
    {
        pmsnk->ReportLine(PszLit("Memory failure"));
        return pvNil;
    }

    if (pvNil == (_pcfl = ChunkyFile::PcflCreate(pfniDst, fcflWriteEnable)))
    {
        pmsnk->ReportLine(PszLit("Couldn't create destination file"));
        ReleasePpo(&_pchlx);
        return pvNil;
    }

    _pmsnkError = pmsnk;
    _format.string_mode = smStz;
    _format.number_bytes = size(LegacyLong);
    _format.byte_order = kboCur;
    _format.os_kind = koskCur;

    _Assemble();

    if (FError())
    {
        ReleasePpo(&_pcfl);
        pfniDst->FDelete();
    }
    ReleasePpo(&_pchlx);

    pcfl = _pcfl;
    _pcfl = pvNil;
    _pmsnkError = pvNil;
    return pcfl;
}

CompilerLexer::CompilerLexer(PFileByteStream pbsf, PString pstnFile) : CompilerLexer_PAR(pbsf, pstnFile)
{
    mm_chunky_lexer_init(&_chunky, sizeof(achar), KauaiCopyAllocator());
    AssertThis(0);
}

CompilerLexer::~CompilerLexer(void)
{
    mm_chunky_lexer_dispose(&_chunky);
}

bool CompilerLexer::FGetTok(PToken ptok)
{
    return _FReadToken(ptok, &_chunky);
}

/***************************************************************************
    Reads in the next token.  Skips semicolons and commas.
***************************************************************************/
bool CompilerLexer::FGetTokSkipSemi(PToken ptok)
{
    AssertThis(0);
    AssertVarMem(ptok);

    // skip comma and semicolon separators
    while (FGetTok(ptok))
    {
        if (ttComma != ptok->tt && ttSemi != ptok->tt)
            return fTrue;
    }

    return fFalse;
}

/***************************************************************************
    Reads a path and builds an Filename.
***************************************************************************/
bool CompilerLexer::FGetPath(Filename *pfni)
{
    AssertThis(0);
    AssertPo(pfni, 0);
    String stn;

    if (!_FReadPath(&stn))
        return fFalse;

#ifdef WIN
    static achar _szInclude[1024];
    ZString szT;

    if (_szInclude[0] == 0)
    {
        GetEnvironmentVariable(PszLit("include"), _szInclude, CvFromRgv(_szInclude) - 1);
    }

    if (0 != SearchPath(_szInclude, stn.Psz(), pvNil, kcchMaxSz, szT, pvNil))
        stn = szT;
    return pfni->FBuildFromPath(&stn);
#endif // WIN
#ifdef MAC
    return pfni->FBuild(0, 0, &stn, kftgText);
#endif // MAC
#if !defined(WIN) && !defined(MAC)
    // Authored chunky sources use Windows separators on every host. Resolve
    // them through the selected file backend, not a Windows-only lexer path.
    for (LegacyLong i = 0; i < stn.Cch(); ++i)
        if (stn.Prgch()[i] == '\\') stn.Prgch()[i] = KauaiFiles::Separator();
    return pfni->FBuildFromPath(&stn) && pfni->FResolveCase();
#endif
}

#ifdef DEBUG
/***************************************************************************
    Assert that the CompilerLexer is a valid object.
***************************************************************************/
void CompilerLexer::AssertValid(LegacyUlong grf)
{
    CompilerLexer_PAR::AssertValid(grf);
    Assert(_chunky.variables.unit_bytes == sizeof(achar), "bad Chunky lexer text width");
}

/***************************************************************************
    Mark memory for the CompilerLexer object.
***************************************************************************/
void CompilerLexer::MarkMem(void)
{
    AssertValid(0);
    CompilerLexer_PAR::MarkMem();
    MarkPv(_chunky.storage.buffers[0].data);
    MarkPv(_chunky.storage.buffers[1].data);
}
#endif

/***************************************************************************
    Constructor for the Decompiler class. This is the chunky decompiler.
***************************************************************************/
Decompiler::Decompiler(void)
{
    _ert = ertNil;
    _pcfl = pvNil;
    AssertThis(0);
}

/***************************************************************************
    Destructor for the Decompiler class.
***************************************************************************/
Decompiler::~Decompiler(void)
{
    ReleasePpo(&_pcfl);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a Decompiler.
***************************************************************************/
void Decompiler::AssertValid(LegacyUlong grf)
{
    Decompiler_PAR::AssertValid(0);
    AssertNilOrPo(_pcfl, 0);
    AssertPo(&_bsf, 0);
    AssertPo(&_chse, 0);
}

/***************************************************************************
    Mark memory for the Decompiler.
***************************************************************************/
void Decompiler::MarkMem(void)
{
    AssertValid(0);
    Decompiler_PAR::MarkMem();
    MarkMemObj(&_bsf);
    MarkMemObj(&_chse);
}
#endif // DEBUG

/***************************************************************************
    Decompile a chunky file.
***************************************************************************/
bool Decompiler::FDecompile(PChunkyFile pcflSrc, PMSNK pmsnk, PMSNK pmsnkError)
{
    AssertThis(0);
    AssertPo(pcflSrc, 0);
    LegacyLong icki, ikid, ckid;
    ChunkTagOrType ctg;
    ChunkIdentification cki;
    ChildChunkIdentification kid;
    DataBlock blck;

    _pcfl = pcflSrc;
    _ert = ertNil;
    _chse.Init(pmsnk, pmsnkError);

    _chse.DumpSz(PszLit("BYTE"));
    _chse.DumpSz(PszLit(""));
    for (icki = 0; _pcfl->FGetCki(icki, &cki, pvNil, &blck); icki++)
    {
        String stnName;

        // don't dump these, because they're embedded in the script
        if (cki.ctg == kctgScriptStrs)
            continue;

        _pcfl->FGetName(cki.ctg, cki.cno, &stnName);
        _chse.DumpHeader(cki.ctg, cki.cno, &stnName);

        // look for special ChunkTags
        ctg = cki.ctg;

        // handle 4 character ctg's
        switch (ctg)
        {
        case kctgScript:
            if (_FDumpScript(&cki))
                goto LEndChunk;
            _pcfl->FGetCki(icki, &cki, pvNil, &blck);
            break;
        }

        {
            const auto result = _chse.DumpCollection(cki.ctg, &blck);
            if (result == SourceEmitter::CollectionResult::Dumped)
                goto LEndChunk;
            if (result == SourceEmitter::CollectionResult::Invalid)
                _pcfl->FGetCki(icki, &cki, pvNil, &blck);
        }

        _chse.DumpBlck(&blck);

    LEndChunk:
        _chse.DumpSz(PszLit("ENDCHUNK"));
        _chse.DumpSz(PszLit(""));
    }

    // now output parent-child relationships
    for (icki = 0; _pcfl->FGetCki(icki++, &cki, &ckid);)
    {
        for (ikid = 0; ikid < ckid;)
        {
            AssertDo(_pcfl->FGetKid(cki.ctg, cki.cno, ikid++, &kid), 0);
            if (kid.cki.ctg == kctgScriptStrs && cki.ctg == kctgScript)
                continue;
            _chse.DumpAdoptCmd(&cki, &kid);
        }
    }
    _pcfl = pvNil;
    _chse.Uninit();

    return !FError();
}

/***************************************************************************
    Disassemble the script and dump it.
***************************************************************************/
bool Decompiler::_FDumpScript(ChunkIdentification *pcki)
{
    AssertThis(0);
    AssertVarMem(pcki);
    PScript pscpt;
    bool fRet;
    GraphicsObjectCompiler sccg;
    LegacyLong cfmt;
    bool fPacked;
    DataBlock blck;

    _pcfl->FFind(pcki->ctg, pcki->cno, &blck);
    fPacked = blck.FPacked(&cfmt);

    if (pvNil == (pscpt = ScriptInterpreter::ReadScript(_pcfl, pcki->ctg, pcki->cno)))
        return fFalse;

    if (fPacked)
        _chse.DumpPack(cfmt);

    fRet = _chse.FDumpScript(pscpt, &sccg);

    ScriptInterpreter::ReleaseScript(&pscpt);

    return fRet;
}

} // end of namespace Chunky
