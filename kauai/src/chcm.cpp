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
#include "mm/chunky_collection.h"
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
    _pglcsfc = pvNil;
    _pcfl = pvNil;
    _pchlx = pvNil;
    _pglckiLoner = pvNil;
    _pmsnkError = pvNil;
    _cactError = 0;
    AssertThis(0);
}

/***************************************************************************
    Destructor for the Compiler class.
***************************************************************************/
Compiler::~Compiler(void)
{
    if (pvNil != _pglcsfc)
    {
        CSFC csfc;

        while (_pglcsfc->FPop(&csfc))
        {
            ReleasePpo(&_pcfl);
            _pcfl = csfc.pcfl;
        }
        ReleasePpo(&_pglcsfc);
    }
    ReleasePpo(&_pcfl);
    ReleasePpo(&_pchlx);
    ReleasePpo(&_pglckiLoner);
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
    AssertNilOrPo(_pglckiLoner, 0);
    AssertNilOrPo(_pmsnkError, 0);
}

/***************************************************************************
    Mark memory for the Compiler object.
***************************************************************************/
void Compiler::MarkMem(void)
{
    AssertThis(0);

    Compiler_PAR::MarkMem();
    MarkMemObj(_pglcsfc);
    MarkMemObj(_pcfl);
    MarkMemObj(&_bsf);
    MarkMemObj(_pchlx);
    MarkMemObj(_pglckiLoner);
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
    source.set_text = [](void *destination, const void *text, MMU32 count) {
        auto string = static_cast<PString>(destination);
        if (count)
            string->SetRgch(const_cast<achar *>(static_cast<const achar *>(text)), count);
        else
            string->SetNil();
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
    Checks if data is already in the buffer (and issues an error) for a
    non-buffer command such as metafile import.
***************************************************************************/
void Compiler::_ErrorOnData(PZString pszPreceed)
{
    AssertThis(0);
    AssertSz(pszPreceed);

    if (_bsf.IbMac() > 0)
    {
        // already data
        _Error(ertBufData, pszPreceed);

        // clear buffer
        _bsf.FReplace(pvNil, 0, 0, _bsf.IbMac());
    }
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
    Parse a chunk header from the source file.
***************************************************************************/
void Compiler::_ParseChunkHeader(ChunkTagOrType *pctg, ChunkNumber *pcno)
{
    AssertThis(0);
    AssertVarMem(pctg);
    AssertVarMem(pcno);

    String stnChunkName;
    PHP rgphp[3];
    LegacyLong cphp;

    ClearPb(rgphp, size(rgphp));
    rgphp[2].text = &stnChunkName;
    if (!_FParseParenHeader(rgphp, 3, &cphp) || cphp < 2)
    {
        _Error(ertChunkHead);
        goto LFail;
    }

    *pctg = rgphp[0].number;
    *pcno = rgphp[1].number;

    // write empty chunk
    if (_pcfl->FFind(*pctg, *pcno))
    {
        // duplicate chunk!
        _Error(ertDupChunk);
    LFail:
        _SkipPastTok(ttEndChunk);
        TrashVar(pctg);
        TrashVar(pcno);
        return;
    }

    // create the chunk and set its name
    if (!_pcfl->FPutPv(pvNil, 0, *pctg, *pcno) ||
        stnChunkName.Cch() > 0 && !FError() && !_pcfl->FSetName(*pctg, *pcno, &stnChunkName))
    {
        _Error(ertOom);
    }
}

/***************************************************************************
    Parse a child statement from the source file.
***************************************************************************/
void Compiler::_ParseBodyChild(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    ChunkTagOrType ctgChild;
    ChunkNumber cnoChild;
    ChildChunkID chid;
    PHP rgphp[3];
    LegacyLong cphp;

    ClearPb(rgphp, size(rgphp));
    if (!_FParseParenHeader(rgphp, 3, &cphp) || cphp < 2)
    {
        _Error(ertBodyChildHead);
        return;
    }

    ctgChild = rgphp[0].number;
    cnoChild = rgphp[1].number;
    chid = rgphp[2].number;

    // check if chunk exists
    if (!_pcfl->FFind(ctgChild, cnoChild))
    {
        _Error(ertChildMissing);
        return;
    }

    // check if cycle would be created
    if (_pcfl->TIsDescendent(ctgChild, cnoChild, ctg, cno) != tNo)
    {
        _Error(ertCycle);
        return;
    }

    // do the adoption
    if (!FError() && !_pcfl->FAdoptChild(ctg, cno, ctgChild, cnoChild, chid, fTrue))
    {
        _Error(ertOom);
    }
}

/***************************************************************************
    Parse a parent statement from the source file.
***************************************************************************/
void Compiler::_ParseBodyParent(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    ChunkTagOrType ctgParent;
    ChunkNumber cnoParent;
    ChildChunkID chid;
    PHP rgphp[3];
    LegacyLong cphp;

    ClearPb(rgphp, size(rgphp));
    if (!_FParseParenHeader(rgphp, 3, &cphp) || cphp < 2)
    {
        _Error(ertBodyParentHead);
        return;
    }

    ctgParent = rgphp[0].number;
    cnoParent = rgphp[1].number;
    chid = rgphp[2].number;

    // check if chunk exists
    if (!_pcfl->FFind(ctgParent, cnoParent))
    {
        _Error(ertParentMissing);
        return;
    }

    // check if cycle would be created
    if (_pcfl->TIsDescendent(ctg, cno, ctgParent, cnoParent) != tNo)
    {
        _Error(ertCycle);
        return;
    }

    // do the adoption
    if (!FError() && !_pcfl->FAdoptChild(ctgParent, cnoParent, ctg, cno, chid, fTrue))
    {
        _Error(ertOom);
    }
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

/***************************************************************************
    Parse a data section from the source file.  ptok should be pre-loaded
    with the first token and when _FParseData returns it contains the next
    token to be processed.  Returns false iff no tokens were consumed.
***************************************************************************/
bool Compiler::_FParseData(PToken ptok)
{
    AssertThis(0);
    AssertVarMem(ptok);
    SourceCursor cursor{this, ptok};
    const auto source = _ChunkySource(&cursor);
    auto token = _SourceToken(ptok);
    const bool consumed = mm_chunky_parse_data(&source, &token) != 0;
    ptok->tt = token.kind;
    return consumed;
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

/***************************************************************************
    Parse a script from the source file.
***************************************************************************/
void Compiler::_ParseBodyPackedFile(bool *pfPacked)
{
    AssertThis(0);
    LegacyLong lw, lwSwapped;
    Token tok;

    _ParseBodyFile();
    if (_bsf.IbMac() < size(LegacyLong))
    {
        _Error(ertPackedFile, PszLit("bad packed file"));
        _SkipPastTok(ttEndChunk);
        return;
    }

    _bsf.FetchRgb(0, size(LegacyLong), &lw);
    lwSwapped = lw;
    SwapBytesRglw(&lwSwapped, 1);
    if (lw == klwSigPackedFile || lwSwapped == klwSigPackedFile)
        *pfPacked = fTrue;
    else if (lw == klwSigUnpackedFile || lwSwapped == klwSigUnpackedFile)
        *pfPacked = fFalse;
    else
    {
        _Error(ertPackedFile, PszLit("not a packed file"));
        _SkipPastTok(ttEndChunk);
        return;
    }

    _bsf.FReplace(pvNil, 0, 0, size(LegacyLong));

    if (!_FGetCleanTok(&tok) || ttEndChunk != tok.tt)
    {
        _Error(ertNeedEndChunk);
        _SkipPastTok(ttEndChunk);
    }
}

/***************************************************************************
    Start a sub file.
***************************************************************************/
void Compiler::_StartSubFile(bool fPack, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    CSFC csfc;

    if (pvNil == _pglcsfc && pvNil == (_pglcsfc = DynamicArray::PglNew(size(CSFC))))
        goto LFail;

    csfc.pcfl = _pcfl;
    csfc.ctg = ctg;
    csfc.cno = cno;
    csfc.fPack = FPure(fPack);

    if (!_pglcsfc->FPush(&csfc))
        goto LFail;

    if (pvNil == (_pcfl = ChunkyFile::PcflCreateTemp()))
    {
        _pglcsfc->FPop();
        _pcfl = csfc.pcfl;
    LFail:
        _Error(ertOom);
    }
}

/***************************************************************************
    End a sub file.
***************************************************************************/
void Compiler::_EndSubFile(void)
{
    AssertThis(0);
    CSFC csfc;

    if (pvNil == _pglcsfc || !_pglcsfc->FPop(&csfc))
    {
        _Error(ertSyntax);
        return;
    }

    AssertPo(csfc.pcfl, 0);

    if (!FError())
    {
        LegacyLong icki;
        ChunkIdentification cki;
        LegacyLong cbTot, cbT;
        FilePosition fpDst;
        PFileObject pfilDst = pvNil;
        bool fRet = fFalse;

        // get the size of the data
        cbTot = 0;
        for (icki = 0; _pcfl->FGetCki(icki, &cki); icki++)
        {
            if (_pcfl->CckiRef(cki.ctg, cki.cno) > 0)
                continue;
            if (!_pcfl->FWriteChunkTree(cki.ctg, cki.cno, pvNil, 0, &cbT))
                goto LFail;
            cbTot += cbT;
        }

        // setup pfilDst and fpDst for writing the chunk trees
        if (csfc.fPack)
        {
            pfilDst = FileObject::PfilCreateTemp();
            fpDst = 0;
        }
        else
        {
            FileLocation floDst;

            // resize the chunk
            if (!csfc.pcfl->FPut(cbTot, csfc.ctg, csfc.cno))
                goto LFail;
            csfc.pcfl->FFindFlo(csfc.ctg, csfc.cno, &floDst);

            pfilDst = floDst.pfil;
            pfilDst->AddRef();
            fpDst = floDst.fp;
        }

        // write the data to (pfilDst, fpDst)
        for (icki = 0; _pcfl->FGetCki(icki, &cki); icki++)
        {
            if (_pcfl->CckiRef(cki.ctg, cki.cno) > 0)
                continue;
            if (!_pcfl->FWriteChunkTree(cki.ctg, cki.cno, pfilDst, fpDst, &cbT))
                goto LFail;
            fpDst += cbT;
            Debug(cbTot -= cbT;)
        }
        Assert(cbTot == 0, "FWriteChunkTree messed up!");

        if (csfc.fPack)
        {
            DataBlock blck(pfilDst, 0, fpDst);

            // pack the data and put it in the chunk.
            blck.FPackData();
            if (!csfc.pcfl->FPutBlck(&blck, csfc.ctg, csfc.cno))
                goto LFail;
        }

        csfc.pcfl->SetForest(csfc.ctg, csfc.cno, fTrue);
        fRet = fTrue;

    LFail:
        if (!fRet)
            _Error(ertOom);

        ReleasePpo(&pfilDst);
    }

    ReleasePpo(&_pcfl);
    _pcfl = csfc.pcfl;
}

/***************************************************************************
    Parse a PACKFMT command, which is used to specify the packing format
    to use.
***************************************************************************/
void Compiler::_ParsePackFmt(void)
{
    AssertThis(0);
    PHP rgphp[1];
    LegacyLong cphp;
    LegacyLong cfmt;

    // get the format
    ClearPb(rgphp, size(rgphp));
    if (!_FParseParenHeader(rgphp, 1, &cphp) || cphp < 1)
    {
        _Error(ertSyntax);
        return;
    }

    cfmt = rgphp[0].number;
    if (!vpcodmUtil->FCanDo(cfmt, fTrue))
        _Error(ertBadPackFmt);
    else
        vpcodmUtil->SetCfmtDefault(cfmt);
}

/***************************************************************************
    Parse the chunk body from the source file.
***************************************************************************/
void Compiler::_ParseChunkBody(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    Token tok;
    DataBlock blck;
    ChunkIdentification cki;
    bool fFetch;
    bool fPack, fPrePacked;

    // empty the FileByteStream
    _bsf.FReplace(pvNil, 0, 0, _bsf.IbMac());

    fFetch = fTrue;
    fPack = fPrePacked = fFalse;
    for (;;)
    {
        if (fFetch && !_FGetCleanTok(&tok))
            return;

        fFetch = fTrue;
        switch (tok.tt)
        {
        default:
            if (!_FParseData(&tok))
            {
                _Error(ertBadToken);
                _SkipPastTok(ttEndChunk);
                return;
            }
            // don't fetch next token
            fFetch = fFalse;
            break;
        case ttChild:
            _ParseBodyChild(ctg, cno);
            break;
        case ttParent:
            _ParseBodyParent(ctg, cno);
            break;
        case ttLoner:
            if (pvNil != _pglcsfc && 0 < _pglcsfc->IvMac())
            {
                _Error(ertLonerInSub);
                break;
            }
            cki.ctg = ctg;
            cki.cno = cno;
            if (pvNil == _pglckiLoner && pvNil == (_pglckiLoner = DynamicArray::PglNew(size(ChunkIdentification))) || !_pglckiLoner->FPush(&cki))
            {
                _Error(ertOom);
            }
            break;

        case ttPrePacked:
            fPrePacked = fTrue;
            break;

        case ttPack:
            fPack = fTrue;
            break;

        case ttPackFmt:
            _ParsePackFmt();
            break;

            // We're done after all the cases below
        case ttPackedFile:
            fPack = fFalse;
            _ErrorOnData(PszLit("Packed File"));
            _ParseBodyPackedFile(&fPrePacked);
            // fall thru
        case ttEndChunk:
            if (!FError())
            {
                if (!_FPrepWrite(fPack, _bsf.IbMac(), ctg, cno, &blck) || !_bsf.FWriteRgb(&blck) ||
                    !_FEndWrite(fPack, ctg, cno, &blck))
                {
                    _Error(ertOom);
                }
            }
            _bsf.FReplace(pvNil, 0, 0, _bsf.IbMac());
            if (!FError() && fPrePacked)
                _pcfl->SetPacked(ctg, cno, fTrue);
            return;
        case ttMeta:
            _ErrorOnData(PszLit("Metafile"));
            _ParseBodyMeta(fPack, ctg, cno);
            return;
        case ttBitmap:
        case ttMask:
            _ErrorOnData(PszLit("Bitmap"));
            _ParseBodyBitmap(fPack, ttMask == tok.tt, ctg, cno);
            return;
        case ttPalette:
            _ErrorOnData(PszLit("Palette"));
            _ParseBodyPalette(fPack, ctg, cno);
            return;
        case ttMidi:
            _ErrorOnData(PszLit("Midi"));
            _ParseBodyMidi(fPack, ctg, cno);
            return;
        case ttCursor:
            _ErrorOnData(PszLit("Cursor"));
            _ParseBodyCursor(fPack, ctg, cno);
            return;
        case ttAl:
        case ttGl:
            _ErrorOnData(PszLit("List"));
            _ParseBodyCollection(fPack, tok.tt, ctg, cno);
            return;
        case ttAg:
        case ttGg:
            _ErrorOnData(PszLit("Group"));
            _ParseBodyCollection(fPack, tok.tt, ctg, cno);
            return;
        case ttAst:
        case ttGst:
            _ErrorOnData(PszLit("String Table"));
            _ParseBodyCollection(fPack, tok.tt, ctg, cno);
            return;
        case ttScript:
        case ttScriptP:
            _ErrorOnData(PszLit("Script"));
            _ParseBodyScript(fPack, ttScript == tok.tt, ctg, cno);
            return;
        case ttSubFile:
            _ErrorOnData(PszLit("Sub File"));
            _StartSubFile(fPack, ctg, cno);
            return;
        }
    }
}

/***************************************************************************
    Parse an adopt parenthesized header from the source file.
***************************************************************************/
void Compiler::_ParseAdopt(void)
{
    AssertThis(0);
    ChunkTagOrType ctgParent, ctgChild;
    ChunkNumber cnoParent, cnoChild;
    ChildChunkID chid;
    PHP rgphp[5];
    LegacyLong cphp;

    ClearPb(rgphp, size(rgphp));
    if (!_FParseParenHeader(rgphp, 5, &cphp) || cphp < 4)
    {
        _Error(ertAdoptHead);
        return;
    }

    ctgParent = rgphp[0].number;
    cnoParent = rgphp[1].number;
    ctgChild = rgphp[2].number;
    cnoChild = rgphp[3].number;
    chid = rgphp[4].number;

    // check if parent exists
    if (!_pcfl->FFind(ctgParent, cnoParent))
    {
        _Error(ertParentMissing);
        return;
    }

    // check if child exists
    if (!_pcfl->FFind(ctgChild, cnoChild))
    {
        _Error(ertChildMissing);
        return;
    }

    // check if cycle would be created
    if (_pcfl->TIsDescendent(ctgChild, cnoChild, ctgParent, cnoParent) != tNo)
        _Error(ertCycle);
    else if (!FError() && !_pcfl->FAdoptChild(ctgParent, cnoParent, ctgChild, cnoChild, chid, fTrue))
    {
        _Error(ertOom);
    }
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
    Token tok;
    ChunkTagOrType ctg;
    ChunkNumber cno;
    PChunkyFile pcfl;
    bool fReportBadTok;

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

    fReportBadTok = fTrue;
    while (_FGetCleanTok(&tok, fTrue))
    {
        switch (tok.tt)
        {
        case ttChunk:
            _ParseChunkHeader(&ctg, &cno);
            _ParseChunkBody(ctg, cno);
            fReportBadTok = fTrue;
            break;

        case ttEndChunk:
            // ending a sub file
            _EndSubFile();
            fReportBadTok = fTrue;
            break;

        case ttAdopt:
            _ParseAdopt();
            fReportBadTok = fTrue;
            break;

        case ttPackFmt:
            _ParsePackFmt();
            fReportBadTok = fTrue;
            break;

        default:
            if (fReportBadTok)
            {
                _Error(ertNeedChunk);
                fReportBadTok = fFalse;
            }
            break;
        }

        if (_cactError > 100)
        {
            pmsnk->ReportLine(PszLit("Too many errors - compilation aborted"));
            break;
        }
    }

    // empty the FileByteStream
    _bsf.FReplace(pvNil, 0, 0, _bsf.IbMac());

    // make sure we're not in any subfiles
    if (pvNil != _pglcsfc && _pglcsfc->IvMac() > 0)
    {
        CSFC csfc;

        _Error(ertNoEndSubFile);
        while (_pglcsfc->FPop(&csfc))
        {
            ReleasePpo(&_pcfl);
            _pcfl = csfc.pcfl;
        }
    }

    if (!FError() && pvNil != _pglckiLoner)
    {
        ChunkIdentification cki;
        LegacyLong icki;

        for (icki = _pglckiLoner->IvMac(); icki-- > 0;)
        {
            _pglckiLoner->Get(icki, &cki);
            _pcfl->SetLoner(cki.ctg, cki.cno, fTrue);
        }
    }

    if (!FError() && !_pcfl->FSave(kctgChkCmp, pvNil))
        _Error(ertOom);

    if (FError())
    {
        ReleasePpo(&_pcfl);
        pfniDst->FDelete();
    }
    ReleasePpo(&_pchlx);
    ReleasePpo(&_pglckiLoner);

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

    _bo = kboCur;
    _osk = koskCur;

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

        // handle 3 character ctg's
        ctg = ctg & 0xFFFFFF00L | 0x00000020L;
        switch (ctg)
        {
        case kctgGst:
        case kctgAst:
            if (_FDumpStringTable(&blck, kctgAst == ctg))
                goto LEndChunk;
            _pcfl->FGetCki(icki, &cki, pvNil, &blck);
            break;
        }

        // handle 2 character ctg's
        ctg = ctg & 0xFFFF0000L | 0x00002020L;
        switch (ctg)
        {
        case kctgGl:
        case kctgAl:
            if (_FDumpList(&blck, kctgAl == ctg))
                goto LEndChunk;
            _pcfl->FGetCki(icki, &cki, pvNil, &blck);
            break;
        case kctgGg:
        case kctgAg:
            if (_FDumpGroup(&blck, kctgAg == ctg))
                goto LEndChunk;
            _pcfl->FGetCki(icki, &cki, pvNil, &blck);
            break;
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
        _WritePack(cfmt);

    fRet = _chse.FDumpScript(pscpt, &sccg);

    ScriptInterpreter::ReleaseScript(&pscpt);

    return fRet;
}

/***************************************************************************
    Try to read the chunk as a list and dump it out.  If the chunk isn't
    a list, return false so it can be dumped in hex.
***************************************************************************/
bool Decompiler::_FDumpList(PDataBlock pblck, bool fAl)
{
    AssertThis(0);
    AssertPo(pblck, fblckReadable);

    PVirtualArray pglb;
    short bo, osk;
    LegacyLong cfmt;
    bool fPacked = pblck->FPacked(&cfmt);

    pglb = fAl ? (PVirtualArray)AllocatedArray::PalRead(pblck, &bo, &osk) : (PVirtualArray)DynamicArray::PglRead(pblck, &bo, &osk);
    if (pvNil == pglb)
        return fFalse;

    if (fPacked)
        _WritePack(cfmt);

    if (bo != _bo)
    {
        _chse.DumpSz(const_cast<PZString>(MacWin(bo != kboCur, bo == kboCur) ? PszLit("WINBO") : PszLit("MACBO")));
        _bo = bo;
    }
    if (osk != _osk)
    {
        _chse.DumpSz(const_cast<PZString>(osk == koskWin ? PszLit("WINOSK") : PszLit("MACOSK")));
        _osk = osk;
    }

    _chse.DumpList(pglb);
    ReleasePpo(&pglb);

    return fTrue;
}

/***************************************************************************
    Try to read the chunk as a group and dump it out.  If the chunk isn't
    a group, return false so it can be dumped in hex.
***************************************************************************/
bool Decompiler::_FDumpGroup(PDataBlock pblck, bool fAg)
{
    AssertThis(0);
    AssertPo(pblck, fblckReadable);

    PVirtualGroup pggb;
    short bo, osk;
    LegacyLong cfmt;
    bool fPacked = pblck->FPacked(&cfmt);

    pggb = fAg ? (PVirtualGroup)AllocatedGroup::PagRead(pblck, &bo, &osk) : (PVirtualGroup)GeneralGroup::PggRead(pblck, &bo, &osk);
    if (pvNil == pggb)
        return fFalse;

    if (fPacked)
        _WritePack(cfmt);

    if (bo != _bo)
    {
        _chse.DumpSz(const_cast<PZString>(MacWin(bo != kboCur, bo == kboCur) ? PszLit("WINBO") : PszLit("MACBO")));
        _bo = bo;
    }
    if (osk != _osk)
    {
        _chse.DumpSz(const_cast<PZString>(osk == koskWin ? PszLit("WINOSK") : PszLit("MACOSK")));
        _osk = osk;
    }

    _chse.DumpGroup(pggb);
    ReleasePpo(&pggb);

    return fTrue;
}

/***************************************************************************
    Try to read the chunk as a string table and dump it out.  If the chunk
    isn't a string table, return false so it can be dumped in hex.
***************************************************************************/
bool Decompiler::_FDumpStringTable(PDataBlock pblck, bool fAst)
{
    AssertThis(0);
    AssertPo(pblck, fblckReadable);

    PVirtualStringTable pgstb;
    short bo, osk;
    LegacyLong cfmt;
    bool fPacked = pblck->FPacked(&cfmt);
    bool fRet;

    pgstb = fAst ? (PVirtualStringTable)AllocatedStringTable::PastRead(pblck, &bo, &osk) : (PVirtualStringTable)StringTable_GST::PgstRead(pblck, &bo, &osk);
    if (pvNil == pgstb)
        return fFalse;

    if (fPacked)
        _WritePack(cfmt);

    if (bo != _bo)
    {
        _chse.DumpSz(const_cast<PZString>(MacWin(bo != kboCur, bo == kboCur) ? PszLit("WINBO") : PszLit("MACBO")));
        _bo = bo;
    }
    if (osk != _osk)
    {
        _chse.DumpSz(const_cast<PZString>(osk == koskWin ? PszLit("WINOSK") : PszLit("MACOSK")));
        _osk = osk;
    }

    fRet = _chse.FDumpStringTable(pgstb);
    ReleasePpo(&pgstb);

    return fRet;
}

/***************************************************************************
    Write out the PACKFMT and PACK commands
***************************************************************************/
void Decompiler::_WritePack(LegacyLong cfmt)
{
    AssertThis(0);
    String stn;

    if (cfmtNil == cfmt)
        _chse.DumpSz(PszLit("PACK"));
    else
    {
        stn.FFormatSz(PszLit("PACKFMT (0x%x) PACK"), cfmt);
        _chse.DumpSz(stn.Psz());
    }
}

} // end of namespace Chunky
