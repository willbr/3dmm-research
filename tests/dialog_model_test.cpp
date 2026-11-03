/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dialog_session.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "dlg.h"
#undef size
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Shared dialog values must run without native SDK headers
#endif
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *, LegacyLong, schar *, void *, LegacyLong)
{
    std::abort();
}
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Dialog model check failed at %d\n", __LINE__);                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

// Minimal resource-loading host. All value operations below run against the
// production Dialog implementation, linked only to kauai-core. Any attempted
// control/window access aborts rather than silently emulating native behavior.
static KauaiDialogs::Resource TestResource()
{
    KauaiDialogs::Resource resource;
    resource.bounds = {0, 0, 200, 140};
    for (unsigned id : {2, 10, 20, 21, 30, 40})
    {
        KauaiDialogs::Control control;
        control.id = id;
        control.windowClass.ordinal = true;
        control.windowClass.number = id == 30 ? 0x81 : id == 40 ? 0x85 : 0x80;
        control.style = id == 40 ? 3 : id == 10 ? 3 : id == 20 ? 9 | 0x20000 : id == 21 ? 9 : 0;
        resource.controls.push_back(control);
    }
    return resource;
}
bool Dialog::_FInit()
{
    return _FInitItems(TestResource());
}
class NativeDialogAdapter
{
  public:
    static void Attach(PDialog dialog, KauaiDialogs::Session *session)
    {
        dialog->_session = session;
    }
};
LegacyLong Dialog::_LwGetRadioGroup(LegacyLong)
{
    std::abort();
}
void Dialog::_SetRadioGroup(LegacyLong, LegacyLong)
{
    std::abort();
}
bool Dialog::_FGetCheckBox(LegacyLong)
{
    std::abort();
}
void Dialog::_SetCheckBox(LegacyLong, bool)
{
    std::abort();
}
void Dialog::_GetEditText(LegacyLong, PString)
{
    std::abort();
}
void Dialog::_SetEditText(LegacyLong, PString)
{
    std::abort();
}
bool Dialog::_FAddToList(LegacyLong, PString)
{
    std::abort();
}
void Dialog::_ClearList(LegacyLong)
{
    std::abort();
}

int main()
{
    auto dialog = Dialog::PdlgNew(1);
    CHECK(dialog && dialog->IvMac() == 5);
    CHECK(dialog->IditFromSit(20) == 2 && dialog->IditFromSit(21) == 2 && dialog->IditFromSit(22) == ivNil);
    CHECK(dialog->Cb(1) == 4 && dialog->Cb(2) == 4 && !dialog->FGetCheck(1) && dialog->LwGetRadio(2) == 0);
    dialog->PutCheck(1, true);
    dialog->PutRadio(2, 1);
    CHECK(dialog->FGetCheck(1) && dialog->LwGetRadio(2) == 1);
    dialog->PutCheck(1, false);
    CHECK(!dialog->FGetCheck(1));
    String text;
    text.SetSz(PszLit("-123"));
    CHECK(dialog->FPutStn(3, &text));
    LegacyLong number = 0;
    bool empty = true;
    CHECK(dialog->FGetLwFromEdit(3, &number, &empty) && !empty && number == -123);
    CHECK(dialog->FPutLwInEdit(3, std::numeric_limits<LegacyLong>::min()));
    CHECK(dialog->FGetLwFromEdit(3, &number, &empty) && number == std::numeric_limits<LegacyLong>::min());
    CHECK(dialog->FPutLwInEdit(3, std::numeric_limits<LegacyLong>::max()));
    CHECK(dialog->FGetLwFromEdit(3, &number, &empty) && number == std::numeric_limits<LegacyLong>::max());
    text.SetNil();
    CHECK(dialog->FPutStn(3, &text));
    CHECK(!dialog->FGetLwFromEdit(3, &number, &empty) && empty && number == 0);
    text.SetSz(PszLit("bad number"));
    CHECK(dialog->FPutStn(3, &text));
    CHECK(!dialog->FGetLwFromEdit(3, &number, &empty) && !empty);
    text.SetSz(PszLit("Selected"));
    CHECK(dialog->FPutStn(4, &text));
    const auto textBytes = dialog->Cb(4);
    text.SetSz(PszLit("First"));
    CHECK(dialog->FAddToList(4, &text));
    text.SetSz(PszLit("Second"));
    CHECK(dialog->FAddToList(4, &text));
    CHECK(dialog->Cb(4) > textBytes);
    String selected;
    dialog->GetStn(4, &selected);
    text.SetSz(PszLit("Selected"));
    CHECK(selected.FEqual(&text));
    text.SetSz(PszLit("Changed"));
    CHECK(dialog->FPutStn(4, &text));
    dialog->GetStn(4, &selected);
    CHECK(selected.FEqual(&text));
    dialog->ClearList(4);
    CHECK(dialog->Cb(4) == text.CbData());
    dialog->GetStn(4, &selected);
    CHECK(selected.FEqual(&text));
    KauaiDialogs::Session session;
    CHECK(session.Init(TestResource()));
    NativeDialogAdapter::Attach(dialog, &session);
    text.SetNil();
    CHECK(dialog->FPutStn(3, &text));
    dialog->SetValues(0, dialog->IvMac());
    CHECK(session.values[2].number == 1 && session.values[3].text.empty() && session.values[4].text == "Changed");
    session.values[1].number = 1;
    session.values[2].number = 0;
    session.values[3].text = "Caf\xC3\xA9";
    session.values[4].text = "Selected again";
    CHECK(dialog->FGetValues(0, dialog->IvMac()));
    CHECK(dialog->FGetCheck(1) && dialog->LwGetRadio(2) == 0);
    dialog->GetStn(3, &selected);
    CHECK(selected.Cch() == 4 && static_cast<byte>(selected.Prgch()[3]) == 0xE9);
    dialog->GetStn(4, &selected);
    CHECK(selected.FEqualSz(PszLit("Selected again")));
    NativeDialogAdapter::Attach(dialog, nullptr);
    ReleasePpo(&dialog);
    using namespace KauaiDialogs;
    for (unsigned control : {1u, 3u, 2u})
    {
        Session assertion;
        CHECK(assertion.InitAssertion("File.cpp line 12: A & B\n12345678"));
        CHECK(assertion.resource.title == u"Assertion Failure");
        CHECK(assertion.cancelControl == 2 && assertion.Item(control) >= 0);
        assertion.Notify(assertion.Item(control));
        const auto expected = control == 1 ? AssertionResult::ignore : control == 3 ? AssertionResult::debugger : AssertionResult::quit;
        CHECK(assertion.closed && assertion.AssertionAnswer() == expected);
    }
    Session escapeAssertion;
    CHECK(escapeAssertion.InitAssertion("Escape matches the original Quit button"));
    escapeAssertion.Cancel();
    CHECK(escapeAssertion.AssertionAnswer() == AssertionResult::quit);
    CHECK(!escapeAssertion.InitAssertion("\xFF") && escapeAssertion.closed && escapeAssertion.AssertionAnswer() == AssertionResult::quit);
    for (auto buttons : {AlertButtons::ok, AlertButtons::okCancel, AlertButtons::yesNo, AlertButtons::yesNoCancel})
    {
        Session alert;
        CHECK(alert.InitAlert("Caf\xC3\xA9 & text\nSecond line", "Question", buttons));
        CHECK(Caption(alert.resource.controls[0].title.text, true) == "Caf\xC3\xA9 & text\nSecond line");
        CHECK(alert.AlertAnswer() == AlertResult::cancel);
        alert.Cancel();
        if (buttons == AlertButtons::yesNo)
        {
            CHECK(!alert.closed);
            alert.Notify(alert.Item(3));
            CHECK(alert.AlertAnswer() == AlertResult::no);
        }
        else
            CHECK(alert.closed && alert.AlertAnswer() == (buttons == AlertButtons::ok ? AlertResult::yes : AlertResult::cancel));
        CHECK(alert.InitAlert("Approve?", "", buttons));
        alert.Notify(alert.Item(1));
        CHECK(alert.AlertAnswer() == AlertResult::yes);
        CHECK(!alert.InitAlert("\xFF", "", buttons) && alert.closed && alert.AlertAnswer() == AlertResult::yes);
    }
    std::puts("Shared dialog model: original bindings, four-byte values, signed numeric limits, empty/invalid edits "
              "and combo text/list preservation passed without native services.");
}
