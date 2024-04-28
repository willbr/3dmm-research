/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    A message sink (MessageSink) wrapper around a stdio file.

***************************************************************************/
#include <stdio.h>
#include "util.h"
#include "mssio.h"
ASSERTNAME

/***************************************************************************
    Constructor for a standard I/O message sink.
***************************************************************************/
MessageSinkIO::MessageSinkIO(FILE *pfile)
{
    _pfile = pfile;
    _fError = fFalse;
}

/***************************************************************************
    Prints a message to stderr.
***************************************************************************/
void MessageSinkIO::ReportLine(PSZ psz)
{
    AssertThis(0);
    AssertSz(psz);

#ifdef UNICODE
    _fError |= 0 > fwprintf(_pfile, PszLit("%s\n"), psz);
#else
    _fError |= 0 > fprintf(_pfile, "%s\n", psz);
#endif
}

/***************************************************************************
    Dump a line to stdout.
***************************************************************************/
void MessageSinkIO::Report(PSZ psz)
{
    AssertThis(0);
    AssertSz(psz);

#ifdef UNICODE
    _fError |= 0 > fwprintf(_pfile, PszLit("%s"), psz);
#else
    _fError |= 0 > fprintf(_pfile, "%s", psz);
#endif
}

/***************************************************************************
    Return whether there has been an error writing to this message sink.
***************************************************************************/
bool MessageSinkIO::FError(void)
{
    AssertThis(0);
    return _fError;
}
