#include <stdio.h>
#include "frame.h"
#include "mssio.h"
#include "soc.h"

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
// #ifndef CHCM_H
// #define CHCM_H

/***************************************************************************
    Chunky decompiler class.
***************************************************************************/
typedef class MovieDecompiler *PMovieDecompiler;
#define MovieDecompiler_PAR BASE
#define kclsMovieDecompiler 'CHDC'
class MovieDecompiler : public MovieDecompiler_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(MovieDecompiler)

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
    MovieDecompiler(void);
    ~MovieDecompiler(void);

    bool FError(void)
    {
        return ertNil != _ert;
    }

    bool FDecompile(PChunkyFile pcflSrc, PMSNK pmsnk, PMSNK pmsnkError);
    bool FDecompileMovie(PChunkyFile pcflSrc, PMSNK pmsnk, PMSNK pmsnkError);
};

// #endif // CHCM_H
