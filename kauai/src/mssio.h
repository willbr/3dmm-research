/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    A message sink (MessageSink) wrapper around a stdio file.
    Include <stdio.h> before including this file.

***************************************************************************/
#ifndef MSSIO_H
#define MSSIO_H

/***************************************************************************
    Standard i/o message sink.
***************************************************************************/
typedef class MessageSinkIO *PMessageSinkIO;
#define MessageSinkIO_PAR MessageSink
class MessageSinkIO : public MessageSinkIO_PAR
{
  protected:
    bool _fError;
    FILE *_pfile;

  public:
    MessageSinkIO(FILE *pfile);
    virtual void ReportLine(PSTZ pstz);
    virtual void Report(PSTZ pstz);
    virtual bool FError(void);
};

#endif //! MSSIO_H
