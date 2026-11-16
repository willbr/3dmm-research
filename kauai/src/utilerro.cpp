/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Error registration and reporting.  Uses a stack of error code.
    Not based on lists (which seem like an obvious choice), since we don't
    want to do any allocation when registering an error.

***************************************************************************/
#include "util.h"
ASSERTNAME

ErrorStack _ers;
ErrorStack *vpers = &_ers;

RTCLASS(ErrorStack)

namespace {
void ErrorEnter(void *context) { static_cast<Mutex *>(context)->Enter(); }
void ErrorLeave(void *context) { static_cast<Mutex *>(context)->Leave(); }
void ErrorWrite(void *, void *output, MMI32 value) { *static_cast<LegacyLong *>(output) = value; }
#ifdef DEBUG
void ErrorReport(void *, MMI32 code, const char *file, MMI32 line)
{
    String text; SZS bytes;
    text.FFormatSz(PszLit("Error %d"), static_cast<LegacyLong>(code));
    text.GetSzs(bytes);
    WarnProc(const_cast<char *>(file), line, bytes);
}
void ErrorOverflow(void *) { Warn("Warning: error code stack has filled"); }
void ErrorUnderflow(void *, void *output) { TrashVar(static_cast<LegacyLong *>(output)); }
#endif
}
MMErrorStackView ErrorStack::_ErrorView()
{
#ifdef DEBUG
    return mm_error_stack_debug_view(&_errors);
#else
    return mm_error_stack_view(&_errors);
#endif
}
MMErrorStackHost ErrorStack::_ErrorHost()
{
#ifdef DEBUG
    MMErrorStackHost host = {&_mutx, ErrorEnter, ErrorLeave, ErrorReport, ErrorOverflow, ErrorWrite, ErrorUnderflow};
#else
    MMErrorStackHost host = {&_mutx, ErrorEnter, ErrorLeave, nullptr, nullptr, ErrorWrite, nullptr};
#endif
    return host;
}
ErrorStack::ErrorStack(void)
{
    static_assert(kcerdMax == MM_ERROR_CAPACITY, "error stack capacity");
    const auto view = _ErrorView();
    mm_error_stack_init(&view);
}
#ifdef DEBUG
void ErrorStack::Push(LegacyLong erc, PSZS pszsFile, LegacyLong lwLine)
#else
void ErrorStack::Push(LegacyLong erc)
#endif
{
    AssertThis(0);
    const auto view = _ErrorView(); const auto host = _ErrorHost();
#ifdef DEBUG
    mm_error_stack_push(&view, &host, erc, pszsFile, lwLine);
#else
    mm_error_stack_push(&view, &host, erc, nullptr, 0);
#endif
    AssertThis(0);
}
bool ErrorStack::FPop(LegacyLong *perc)
{
    AssertThis(0); AssertNilOrVarMem(perc);
    const auto view = _ErrorView(); const auto host = _ErrorHost();
    const bool result = mm_error_stack_pop(&view, &host, perc) != 0;
    if (result) { AssertThis(0); }
    return result;
}
void ErrorStack::Clear(void)
{
    AssertThis(0);
    const auto view = _ErrorView(); const auto host = _ErrorHost();
    mm_error_stack_clear(&view, &host);
}
LegacyLong ErrorStack::Cerc(void)
{
    AssertThis(0); const auto view = _ErrorView();
    return mm_error_stack_count(&view);
}
bool ErrorStack::FIn(LegacyLong erc)
{
    AssertThis(0);
    const auto view = _ErrorView(); const auto host = _ErrorHost();
    return mm_error_stack_contains(&view, &host, erc) != 0;
}
LegacyLong ErrorStack::ErcGet(LegacyLong ierc)
{
    AssertThis(0);
    const auto view = _ErrorView(); const auto host = _ErrorHost();
    static_assert(static_cast<LegacyLong>(ercNil) == -1, "nil error code");
    return mm_error_stack_get(&view, &host, ierc);
}
void ErrorStack::Flush(LegacyLong erc)
{
    AssertThis(0);
    const auto view = _ErrorView(); const auto host = _ErrorHost();
    mm_error_stack_flush(&view, &host, erc);
}
#ifdef DEBUG
void ErrorStack::AssertValid(LegacyUlong grf)
{
    ErrorStack_PAR::AssertValid(0);
    AssertIn(_errors.count, 0, kcerdMax + 1);
}
#endif
