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

#include "cursor_format.h"

typedef class Cursor *PCursor;
#define Cursor_PAR BaseCacheableObject
#define kclsCursor 'CURS'
class Cursor : public Cursor_PAR
{
    RTCLASS_DEC

  private:
  protected:
#ifdef KAUAI_SDL3
    std::uint64_t _sdlCursor;
#elif defined(WIN)
    HCRS _hcrs;
#endif // WIN
#if defined(MAC) && !defined(KAUAI_SDL3)
    Cursor _crs;
#endif // MAC

    Cursor(void)
    {
    } // we have to be allocated
    ~Cursor(void);

  public:
    static int FReadCurs(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **, MMI32 *);

    void Set(void);
};

#endif //! CURSOR_H
