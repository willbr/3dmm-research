/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Stream classes.

    A MemoryByteStream is a byte stream in memory.  The data is stored contiguously, so
    should be used only for relatively small streams.

    A FileByteStream is a byte stream with pieces stored in files and other pieces
    stored in memory.

***************************************************************************/
#ifndef STREAM_H
#define STREAM_H
#include "three_mm/data_stream.h"
#include "three_mm/piece_table.h"

using Group::PGeneralGroup;

/***************************************************************************
    Byte stream in memory.  The entire stream is in contiguous memory.
***************************************************************************/
typedef class MemoryByteStream *PMemoryByteStream;
#define MemoryByteStream_PAR BASE
#define kclsMemoryByteStream 'BSM'
class MemoryByteStream : public MemoryByteStream_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(MemoryByteStream)

  protected:
    ThreeMMByteBuffer _buffer;

    bool _FEnsureSize(LegacyLong cbMin, bool fShrink);

  public:
    MemoryByteStream(void);
    ~MemoryByteStream(void);

    void SetMinGrow(LegacyLong cb);
    bool FEnsureSpace(LegacyLong cb, bool fShrink);
    void *PvLock(LegacyLong ib);
    void Unlock(void);

    LegacyLong IbMac(void)
    {
        return _buffer.length;
    }
    void FetchRgb(LegacyLong ib, LegacyLong cb, void *prgb);
    bool FReplace(void *prgb, LegacyLong cbIns, LegacyLong ib, LegacyLong cbDel);
    bool FWriteRgb(PFileLocation pflo, LegacyLong ib = 0);
    bool FWriteRgb(PDataBlock pblck, LegacyLong ib = 0);
};

/***************************************************************************
    Byte stream on file.  Parts of the stream may be in files.
***************************************************************************/
typedef class FileByteStream *PFileByteStream;
#define FileByteStream_PAR BASE
#define kclsFileByteStream 'BSF'
class FileByteStream : public FileByteStream_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(FileByteStream)

  protected:
    ThreeMMPieceTable _pieces;
    static void _RetainFile(void *, void *);
    static void _ReleaseFile(void *, void *);
    static void *_CreateTemp(void *);
    static int _ReadFile(void *, const ThreeMMFilePiece *, void *);
    static int _WriteFile(void *, const ThreeMMFilePiece *, const void *);
    static int _CopyFile(void *, const ThreeMMFilePiece *, const ThreeMMFilePiece *);

  public:
    FileByteStream(void);
    ~FileByteStream(void);

    LegacyLong IbMac(void)
    {
        return _pieces.length;
    }
    void FetchRgb(LegacyLong ib, LegacyLong cb, void *prgb);
    bool FReplace(void *prgb, LegacyLong cbIns, LegacyLong ib, LegacyLong cbDel);
    bool FReplaceFlo(PFileLocation pflo, bool fCopy, LegacyLong ib, LegacyLong cbDel);
    bool FReplaceBsf(PFileByteStream pbsfSrc, LegacyLong ibSrc, LegacyLong cbSrc, LegacyLong ibDst, LegacyLong cbDel);
    bool FWriteRgb(PFileLocation pflo, LegacyLong ib = 0);
    bool FWriteRgb(PDataBlock pblck, LegacyLong ib = 0);
    bool FCompact(void);
};

#endif //! STREAM_H
