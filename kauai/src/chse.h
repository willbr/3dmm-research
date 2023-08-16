/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ******
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Header file for the SourceEmitter class - the chunky source emitter class.

***************************************************************************/
#ifndef CHSE_H
#define CHSE_H

namespace Chunky {

using ScriptInterpreter::PScript;
using ScriptCompiler::PCompilerBase;

/***************************************************************************
    Chunky source emitter class
***************************************************************************/
#ifdef DEBUG
enum
{
    fchseNil = 0,
    fchseDump = 0x8000,
};
#endif // DEBUG

typedef class SourceEmitter *PSourceEmitter;
#define SourceEmitter_PAR BASE
#define kclsSourceEmitter 'CHSE'
class SourceEmitter : public SourceEmitter_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(SourceEmitter)

  protected:
    PMSNK _pmsnkDump;
    PMSNK _pmsnkError;
    FileByteStream _bsf;
    bool _fError;

  protected:
    void _DumpBsf(long cactTab);

  public:
    SourceEmitter(void);
    ~SourceEmitter(void);
    void Init(PMSNK pmsnkDump, PMSNK pmsnkError = pvNil);
    void Uninit(void);

    void DumpHeader(ChunkTag ctg, ChunkNumber cno, PSTN pstnName = pvNil, bool fPack = fFalse);
    void DumpRgb(void *prgb, long cb, long cactTab = 1);
    void DumpParentCmd(ChunkTag ctg, ChunkNumber cno, ChildChunkID chid);
    void DumpBitmapCmd(byte bTransparent, long dxp, long dyp, PSTN pstnFile);
    void DumpFileCmd(PSTN pstnFile, bool fPacked = fFalse);
    void DumpAdoptCmd(ChunkIdentification *pcki, ChildChunkIdentification *pkid);
    void DumpList(PVirtualArray pglb);
    void DumpGroup(PVirtualGroup pggb);
    bool FDumpStringTable(PVirtualStringTable pgstb);
    void DumpBlck(PDataBlock pblck);
    bool FDumpScript(PScript pscpt, PCompilerBase psccb);

    // General sz emitting routines
    void DumpSz(PSZ psz)
    {
        AssertThis(fchseDump);
        _pmsnkDump->ReportLine(psz);
    }
    void Error(PSZ psz)
    {
        AssertThis(fchseNil);
        _fError = fTrue;
        if (pvNil != _pmsnkError)
            _pmsnkError->ReportLine(psz);
    }
    bool FError(void)
    {
        return _fError || pvNil != _pmsnkDump && _pmsnkDump->FError();
    }
};

} // end of namespace Chunky

#endif // !CHSE_H
