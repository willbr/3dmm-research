/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    A lexer for command scripts.

***************************************************************************/
#ifndef LEX_H
#define LEX_H
#include "mm/lexer.h"
#include "mm/wide.h"

struct Token
{
    LegacyLong tt;
    LegacyLong lw;
    String stn;
};
typedef Token *PToken;

/***************************************************************************
    Base lexer.
***************************************************************************/
typedef class LexerBase *PLexerBase;
#define LexerBase_PAR BASE
#define kclsLexerBase 'LEXB'
class LexerBase : public LexerBase_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    MMLexer _lexer;
    PFileByteStream _pbsf;
    String _stnFile;

    struct NumberContext
    {
        LexerBase *lexer;
        PToken token;
    };
    static int _ReadInput(void *context, MMU32 offset, void *bytes, size_t count);
    static void _ReadNumberToken(void *context, MMLexToken *token, unsigned first, unsigned base);

  protected:
    PFileObject _pfil; // exactly one of _pfil, _pbsf should be non-nil
    bool _FFetchRgch(achar *pch);
    void _Advance()
    {
        mm_lexer_advance(&_lexer);
    }
    virtual void _ReadNumTok(PToken ptok, achar ch, LegacyLong lwBase, LegacyLong cchMax)
    {
        ptok->lw = mm_lexer_number(&_lexer, static_cast<uchar>(ch), lwBase, cchMax);
    }
    bool _FReadPath(PString path);

  public:
    LexerBase(PFileObject pfil, bool fUnionStrings = fTrue);
    LexerBase(PFileByteStream pbsf, PString pstnFile, bool fUnionStrings = fTrue);
    ~LexerBase(void);

    virtual bool FGetTok(PToken ptok);
    virtual LegacyLong CbExtra(void);
    virtual void GetExtra(void *pv);

    void GetStnFile(PString pstn);
    LegacyLong LwLine(void)
    {
        return mm_signed_word(_lexer.line);
    }
    LegacyLong IchLine(void)
    {
        return _lexer.column;
    }
};

#endif //! LEX_H
