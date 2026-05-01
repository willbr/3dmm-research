/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Cursor class.

***************************************************************************/
#ifndef CURSOR_H
#define CURSOR_H

enum
{
    curtMonochrome = 0,
};

// cursor on file - stored in a GeneralGroup with the rgb's in the variable part
struct CursorOnFile
{
    long curt; // type of cursor
    byte xp;   // hot spot
    byte yp;
    byte dxp; // size - either 16 or 32 and they should match
    byte dyp;
    // byte rgbAnd[];
    // byte rgbXor[];
};
const ByteOrderMask kbomCurf = 0xC0000000;

typedef class Cursor *PCursor;
#define Cursor_PAR BaseCacheableObject
#define kclsCursor 'CURS'
class Cursor : public Cursor_PAR
{
    RTCLASS_DEC

  private:
  protected:
#ifdef WIN
    HCRS _hcrs;
#endif // WIN
#ifdef MAC
    Cursor _crs;
#endif // MAC

    Cursor(void)
    {
    } // we have to be allocated
    ~Cursor(void);

  public:
    static bool FReadCurs(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, DataBlock *pblck, PBaseCacheableObject *ppbaco, long *pcb);

    void Set(void);
};

#endif //! CURSOR_H
