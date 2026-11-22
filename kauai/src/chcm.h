/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Header file for the Compiler class - the chunky compiler class, and
    CompilerLexer - its lexer.

***************************************************************************/
#ifndef CHCM_H
#define CHCM_H
#include "mm/chunky_lexer.h"

namespace Chunky {

// lookup table for keywords
struct LexerKeywordEntry
{
    PZString pszKeyword;
    LegacyLong tt;
};

/***************************************************************************
    Chunky Compiler lexer class.
***************************************************************************/
typedef class CompilerLexer *PCompilerLexer;
#define CompilerLexer_PAR LexerBase
#define kclsCompilerLexer 'CHLX'
class CompilerLexer : public CompilerLexer_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(CompilerLexer)

  private:
    MMChunkyLexer _chunky;

  public:
    CompilerLexer(PFileByteStream pbsf, PString pstnFile);
    ~CompilerLexer(void);

    bool FGetTok(PToken ptok) override;
    virtual bool FGetTokSkipSemi(PToken ptok); // also skip ';' & ','
    virtual bool FGetPath(Filename *pfni);        // read a path
};

// error types
enum
{
    ertNil = 0,
    ertOom,
    ertOpenFile,
    ertReadMeta,
    ertRangeByte,
    ertRangeShort,
    ertBufData,
    ertParenOpen,
    ertEof,
    ertNeedString,
    ertNeedNumber,
    ertBadToken,
    ertParenClose,
    ertChunkHead,
    ertDupChunk,
    ertBodyChildHead,
    ertChildMissing,
    ertCycle,
    ertBodyParentHead,
    ertParentMissing,
    ertBodyAlignRange,
    ertBodyFile,
    ertNeedEndChunk,
    ertListHead,
    ertListEntrySize,
    ertVarUndefined,
    ertItemOverflow,
    ertBadFree,
    ertSyntax,
    ertGroupHead,
    ertGroupEntrySize,
    ertGstHead,
    ertGstEntrySize,
    ertScript,
    ertAdoptHead,
    ertNeedChunk,
    ertBodyBitmapHead,
    ertReadBitmap,
    ertBadScript,
    ertReadCursor,
    ertPackedFile,
    ertReadMidi,
    ertBadPackFmt,
    ertLonerInSub,
    ertNoEndSubFile,
    ertLim
};

#define kcbMinAlign 2
#define kcbMaxAlign 1024

/***************************************************************************
    Base chunky compiler class
***************************************************************************/
typedef class Compiler *PCompiler;
#define Compiler_PAR BASE
#define kclsCompiler 'CHCM'
class Compiler : public Compiler_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(Compiler)

  protected:
    // Chunk sub file context
    struct CSFC
    {
        PChunkyFile pcfl;
        ChunkTagOrType ctg;
        ChunkNumber cno;
        bool fPack;
    };

    PDynamicArray _pglcsfc; // the stack of CSFCs for sub files

    PChunkyFile _pcfl;       // current sub file
    PDynamicArray _pglckiLoner; // the chunks that must be loners

    FileByteStream _bsf;     // temporary buffer for the chunk data
    PCompilerLexer _pchlx; // lexer for compiling
    MMChunkyFormat _format;
    PMSNK _pmsnkError; // error message sink
    LegacyLong _cactError;   // how many errors we've encountered

  protected:
    struct PHP // parenthesized header parameter
    {
        LegacyLong lw;
        PString pstn;
    };

    void _Error(LegacyLong ert, PZString pszMessage = pvNil);
    void _GetRgbFromLw(LegacyLong lw, byte *prgb);
    void _ErrorOnData(PZString pszPreceed);
    bool _FParseParenHeader(PHP *prgphp, LegacyLong cphpMax, LegacyLong *pcphp);
    bool _FGetCleanTok(Token *ptok, bool fEofOk = fFalse);
    void _SkipPastTok(LegacyLong tt);
    void _ParseChunkHeader(ChunkTagOrType *pctg, ChunkNumber *pcno);
    void _AppendString(PString pstnValue);
    void _AppendNumber(LegacyLong lwValue);
    void _ParseBodyChild(ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseBodyParent(ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseBodyAlign(void);
    void _ParseBodyFile(void);

    void _StartSubFile(bool fPack, ChunkTagOrType ctg, ChunkNumber cno);
    void _EndSubFile(void);

    void _ParseBodyMeta(bool fPack, ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseBodyBitmap(bool fPack, bool fMask, ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseBodyPalette(bool fPack, ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseBodyMidi(bool fPack, ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseBodyCursor(bool fPack, ChunkTagOrType ctg, ChunkNumber cno);
    bool _FParseData(PToken ptok);
    void _ParseBodyList(bool fPack, bool fAl, ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseBodyGroup(bool fPack, bool fAg, ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseBodyStringTable(bool fPack, bool fAst, ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseBodyScript(bool fPack, bool fInfix, ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseBodyPackedFile(bool *pfPacked);
    void _ParseChunkBody(ChunkTagOrType ctg, ChunkNumber cno);
    void _ParseAdopt(void);
    void _ParsePackFmt(void);

    bool _FPrepWrite(bool fPack, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck);
    bool _FEndWrite(bool fPack, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck);

  public:
    Compiler(void);
    ~Compiler(void);

    bool FError(void)
    {
        return _cactError > 0;
    }

    PChunkyFile PcflCompile(PFilename pfniSrc, PFilename pfniDst, PMSNK pmsnk);
    PChunkyFile PcflCompile(PFileByteStream pbsfSrc, PString pstnFile, PFilename pfniDst, PMSNK pmsnk);
};

/***************************************************************************
    Chunky decompiler class.
***************************************************************************/
typedef class Decompiler *PDecompiler;
#define Decompiler_PAR BASE
#define kclsDecompiler 'CHDC'
class Decompiler : public Decompiler_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(Decompiler)

  protected:
    LegacyLong _ert;  // error type
    PChunkyFile _pcfl; // the chunky file to read from
    FileByteStream _bsf;   // temporary buffer for the chunk data
    short _bo;  // current byte order and osk
    short _osk;
    SourceEmitter _chse; // chunky source emitter

  protected:
    bool _FDumpScript(ChunkIdentification *pcki);
    bool _FDumpList(PDataBlock pblck, bool fAl);
    bool _FDumpGroup(PDataBlock pblck, bool fAg);
    bool _FDumpStringTable(PDataBlock pblck, bool fAst);
    void _WritePack(LegacyLong cfmt);

  public:
    Decompiler(void);
    ~Decompiler(void);

    bool FError(void)
    {
        return ertNil != _ert;
    }

    bool FDecompile(PChunkyFile pcflSrc, PMSNK pmsnk, PMSNK pmsnkError);
};

} // end of namespace Chunky
#endif // CHCM_H
