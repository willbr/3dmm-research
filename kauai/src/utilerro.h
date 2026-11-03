/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/******************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Error registration and reporting.  All error codes go here.

******************************************************************************/
#ifndef UTILERROR_H
#define UTILERROR_H

const LegacyLong kcerdMax = 20;

/***************************************************************************
    Error stack class
***************************************************************************/
#define ErrorStack_PAR BASE
#define kclsErrorStack 'ERS'
class ErrorStack : public ErrorStack_PAR
{
    RTCLASS_DEC
    ASSERT

  private:
    struct ERD
    {
        LegacyLong erc;
#ifdef DEBUG
        PSZS pszsFile;
        LegacyLong lwLine;
#endif // DEBUG
    };

    Mutex _mutx;
    LegacyLong _cerd;
    ERD _rgerd[kcerdMax];

  public:
    ErrorStack(void);

#ifdef DEBUG
    virtual void Push(LegacyLong erc, schar *pszsFile, LegacyLong lwLine);
#else  //! DEBUG
    virtual void Push(LegacyLong erc);
#endif //! DEBUG
    virtual bool FPop(LegacyLong *perc = pvNil);
    virtual bool FIn(LegacyLong erc);
    virtual LegacyLong Cerc(void);
    virtual LegacyLong ErcGet(LegacyLong ierc);
    virtual void Clear(void);
    virtual void Flush(LegacyLong erc);
};

extern ErrorStack *vpers;

#ifdef DEBUG
#define PushErc(erc) vpers->Push(erc, __szsFile, __LINE__)
#else //! DEBUG
#define PushErc(erc) vpers->Push(erc)
#endif //! DEBUG

#endif //! UTILERROR_H
