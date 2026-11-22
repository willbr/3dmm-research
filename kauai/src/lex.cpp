/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#include "util.h"
#include <cstring>
ASSERTNAME

RTCLASS(LexerBase)

static_assert(kcchMaxStn == MM_LEX_TEXT_CAPACITY, "C lexer and String capacity");
static_assert(sizeof(achar) == 1 || sizeof(achar) == 2, "C lexer text width");

static void CopyLexText(const MMLexText &text, PString out)
{
    achar chars[MM_LEX_TEXT_CAPACITY];
    std::memcpy(chars, &text.units, text.length * sizeof(achar));
    out->SetRgch(chars, text.length);
}

LexerBase::LexerBase(PFileObject pfil, bool fUnionStrings) : _pbsf(pvNil), _pfil(pfil)
{
    AssertPo(pfil, 0);
    _pfil->AddRef();
    _pfil->GetStnPath(&_stnFile);
    const MMReadStream input{this, static_cast<MMU32>(pfil->FpMac()), _ReadInput};
    mm_lexer_init(&_lexer, &input, sizeof(achar), fUnionStrings);
    AssertThis(0);
}

LexerBase::LexerBase(PFileByteStream pbsf, PString pstnFile, bool fUnionStrings) : _pbsf(pbsf), _pfil(pvNil)
{
    AssertPo(pbsf, 0);
    AssertPo(pstnFile, 0);
    _pbsf->AddRef();
    _stnFile = *pstnFile;
    const MMReadStream input{this, static_cast<MMU32>(pbsf->IbMac()), _ReadInput};
    mm_lexer_init(&_lexer, &input, sizeof(achar), fUnionStrings);
    AssertThis(0);
}

LexerBase::~LexerBase(void)
{
    ReleasePpo(&_pfil);
    ReleasePpo(&_pbsf);
}

int LexerBase::_ReadInput(void *context, MMU32 offset, void *bytes, size_t count)
{
    auto lexer = static_cast<LexerBase *>(context);
    if (lexer->_pfil)
    {
        if (lexer->_pfil->FReadRgb(bytes, static_cast<LegacyLong>(count), static_cast<FilePosition>(offset)))
            return 1;
        Warn("Error reading file, truncating logical file");
        return 0;
    }
    lexer->_pbsf->FetchRgb(static_cast<LegacyLong>(offset), static_cast<LegacyLong>(count), bytes);
    return 1;
}

#ifdef DEBUG
void LexerBase::AssertValid(LegacyUlong grf)
{
    LexerBase_PAR::AssertValid(0);
    AssertNilOrPo(_pfil, 0);
    AssertNilOrPo(_pbsf, 0);
    Assert((_pfil == pvNil) != (_pbsf == pvNil), "exactly one of _pfil, _pbsf should be non-nil");
    AssertPo(&_stnFile, 0);
    Assert(_lexer.unit_bytes == sizeof(achar), "bad lexer text width");
    Assert(_lexer.offset <= _lexer.input.length, "bad lexer input bounds");
    Assert(_lexer.begin <= _lexer.end && _lexer.end <= MM_LEX_BUFFER_UNITS, "bad lexer buffer bounds");
}

void LexerBase::MarkMem(void)
{
    AssertValid(0);
    LexerBase_PAR::MarkMem();
    MarkMemObj(_pfil);
    MarkMemObj(_pbsf);
}
#endif

void LexerBase::GetStnFile(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    if (_lexer.has_filename)
        CopyLexText(_lexer.filename, pstn);
    else
        *pstn = _stnFile;
}

bool LexerBase::_FFetchRgch(achar *pch)
{
    unsigned ch;
    if (!mm_lexer_peek(&_lexer, &ch))
        return false;
    *pch = static_cast<achar>(ch);
    return true;
}

bool LexerBase::_FReadPath(PString path)
{
    MMLexText text;
    if (!mm_lexer_path(&_lexer, &text))
        return false;
    CopyLexText(text, path);
    return true;
}

void LexerBase::_ReadNumberToken(void *context, MMLexToken *token, unsigned first, unsigned base)
{
    auto input = static_cast<NumberContext *>(context);
    input->token->tt = ttLong;
    input->lexer->_ReadNumTok(input->token, static_cast<achar>(first), base, klwMax);
    token->kind = input->token->tt;
    if (token->kind == ttLong)
        token->number = input->token->lw;
}

bool LexerBase::FGetTok(PToken ptok)
{
    AssertThis(0);
    AssertVarMem(ptok);
    NumberContext input{this, ptok};
    MMLexToken token;
    ptok->stn.SetNil();
    const bool found = mm_lexer_next(&_lexer, &token, _ReadNumberToken, &input) != 0;
    ptok->tt = token.kind;
    if (token.kind == ttLong)
        ptok->lw = token.number;
    else if (token.text.length)
        CopyLexText(token.text, &ptok->stn);
    return found;
}

LegacyLong LexerBase::CbExtra(void)
{
    AssertThis(0);
    return 0;
}

void LexerBase::GetExtra(void *pv)
{
    AssertThis(0);
    Bug("no extra data");
}
