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

namespace Chunky {

// token types
enum
{
    ttChunk = ttLimBase, // chunk definition
    ttChild,             // child descriptor
    ttParent,            // parent descriptor
    ttAlign,             // align command
    ttFile,              // file import command
    ttMeta,              // metafile import command
    ttBitmap,            // bitmap import command
    ttFree,              // for AllocatedArray, AllocatedGroup, AllocatedStringTable - item is free
    ttItem,              // for DynamicArray, etc - start of data for new item
    ttVar,               // for GeneralGroup and AllocatedGroup - variable sized data
    ttGl,                // DynamicArray command
    ttAl,                // AllocatedArray command
    ttGg,                // GeneralGroup command
    ttAg,                // AllocatedGroup command
    ttGst,               // StringTable command
    ttAst,               // AllocatedStringTable command
    ttScript,            // infix script
    ttScriptP,           // postfix script
    ttModeStn,           // change mode to store strings as stn's
    ttModeStz,           // change mode to store strings as stz's
    ttModeSz,            // change mode to store strings as sz's
    ttModeSt,            // change mode to store strings as st's
    ttModeByte,          // change mode to accept a byte
    ttModeShort,         // change mode to accept a short
    ttModeLong,          // change mode to accept a long
    ttEndChunk,          // end of chunk
    ttAdopt,             // adopt command
    ttMacBo,             // use Mac byte order and OSK
    ttWinBo,             // use Win byte order and OSK
    ttMacOsk,            // use Mac byte order and OSK
    ttWinOsk,            // use Win byte order and OSK
    ttBo,                // insert the current byte order
    ttOsk,               // insert the current OSK
    ttMask,              // MASK command
    ttLoner,             // LONER command
    ttCursor,            // CURSOR command
    ttPalette,           // PALETTE command
    ttPrePacked,         // mark the change as packed
    ttPack,              // pack the data
    ttPackedFile,        // packed file import command
    ttMidi,              // midi file import command
    ttPackFmt,           // format to pack in
    ttSubFile,           // start an embedded chunk forest

    ttLimChlx
};

// lookup table for keywords
struct KEYTT
{
    PSZ pszKeyword;
    long tt;
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

  protected:
    PStringTable _pgstVariables;

    bool _FDoSet(PToken ptok);

  public:
    CompilerLexer(PFileByteStream pbsf, PSTN pstnFile);
    ~CompilerLexer(void);

    // override the LexerBase FGetTok to resolve variables, hande SET
    // and recognize our additional key words
    virtual bool FGetTok(PToken ptok);
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

// string modes
enum
{
    smStn,
    smStz,
    smSz,
    smSt
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
        ChunkTag ctg;
        ChunkNumber cno;
        bool fPack;
    };

    PDynamicArray _pglcsfc; // the stack of CSFCs for sub files

    PChunkyFile _pcfl;       // current sub file
    PDynamicArray _pglckiLoner; // the chunks that must be loners

    FileByteStream _bsf;     // temporary buffer for the chunk data
    PCompilerLexer _pchlx; // lexer for compiling
    long _sm;     // current string mode
    long _cbNum;  // current numerical size (1, 2, or 4)
    short _bo;    // current byte order and osk
    short _osk;
    PMSNK _pmsnkError; // error message sink
    long _cactError;   // how many errors we've encountered

  protected:
    struct PHP // parenthesized header parameter
    {
        long lw;
        PSTN pstn;
    };

    void _Error(long ert, PSZ pszMessage = pvNil);
    void _GetRgbFromLw(long lw, byte *prgb);
    void _ErrorOnData(PSZ pszPreceed);
    bool _FParseParenHeader(PHP *prgphp, long cphpMax, long *pcphp);
    bool _FGetCleanTok(Token *ptok, bool fEofOk = fFalse);
    void _SkipPastTok(long tt);
    void _ParseChunkHeader(ChunkTag *pctg, ChunkNumber *pcno);
    void _AppendString(PSTN pstnValue);
    void _AppendNumber(long lwValue);
    void _ParseBodyChild(ChunkTag ctg, ChunkNumber cno);
    void _ParseBodyParent(ChunkTag ctg, ChunkNumber cno);
    void _ParseBodyAlign(void);
    void _ParseBodyFile(void);

    void _StartSubFile(bool fPack, ChunkTag ctg, ChunkNumber cno);
    void _EndSubFile(void);

    void _ParseBodyMeta(bool fPack, ChunkTag ctg, ChunkNumber cno);
    void _ParseBodyBitmap(bool fPack, bool fMask, ChunkTag ctg, ChunkNumber cno);
    void _ParseBodyPalette(bool fPack, ChunkTag ctg, ChunkNumber cno);
    void _ParseBodyMidi(bool fPack, ChunkTag ctg, ChunkNumber cno);
    void _ParseBodyCursor(bool fPack, ChunkTag ctg, ChunkNumber cno);
    bool _FParseData(PToken ptok);
    void _ParseBodyList(bool fPack, bool fAl, ChunkTag ctg, ChunkNumber cno);
    void _ParseBodyGroup(bool fPack, bool fAg, ChunkTag ctg, ChunkNumber cno);
    void _ParseBodyStringTable(bool fPack, bool fAst, ChunkTag ctg, ChunkNumber cno);
    void _ParseBodyScript(bool fPack, bool fInfix, ChunkTag ctg, ChunkNumber cno);
    void _ParseBodyPackedFile(bool *pfPacked);
    void _ParseChunkBody(ChunkTag ctg, ChunkNumber cno);
    void _ParseAdopt(void);
    void _ParsePackFmt(void);

    bool _FPrepWrite(bool fPack, long cb, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck);
    bool _FEndWrite(bool fPack, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck);

  public:
    Compiler(void);
    ~Compiler(void);

    bool FError(void)
    {
        return _cactError > 0;
    }

    PChunkyFile PcflCompile(PFilename pfniSrc, PFilename pfniDst, PMSNK pmsnk);
    PChunkyFile PcflCompile(PFileByteStream pbsfSrc, PSTN pstnFile, PFilename pfniDst, PMSNK pmsnk);
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
    long _ert;  // error type
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
    void _WritePack(long cfmt);

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
