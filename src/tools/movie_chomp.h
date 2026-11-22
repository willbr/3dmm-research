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
    bool _fError = false;
    SourceEmitter _chse;

  public:
    bool FError(void) const { return _fError; }

    bool FDecompile(PChunkyFile pcflSrc, PMSNK pmsnk, PMSNK pmsnkError);
    bool FDecompileMovie(PChunkyFile pcflSrc, PMSNK pmsnk, PMSNK pmsnkError);
};

// #endif // CHCM_H
