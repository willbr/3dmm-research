/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <exception>
#include "sdl_dialog.h"
#include "sdl_host.h"
#include "dialog_resources.h"
#include "dlg.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The SDL dialog controller must not import the Windows SDK
#endif
ASSERTNAME
#ifndef WIN
bool Dialog::_FReadResource(KauaiDialogs::Resource &model)
{
    if (KauaiDialogs::ReadApplicationResource(_rid, model)) return true;
    PushErc(ercDlgCantFind);
    return false;
}
bool Dialog::_FInit()
{
    KauaiDialogs::Resource model;
    return _FReadResource(model) && _FInitItems(model);
}
LegacyLong Dialog::IditDo(LegacyLong focus)
{
    LegacyLong result;
    return _FDoSDL(focus, result) ? result : ivNil;
}
void Dialog::SelectDit(LegacyLong item)
{
    if (_session) _session->focus = item;
}
// All value operations take the shared Session path in the portable runtime.
// A call without a live session is an invalid API use, never a native fallback.
LegacyLong Dialog::_LwGetRadioGroup(LegacyLong) { Bug("no SDL dialog session"); return 0; }
void Dialog::_SetRadioGroup(LegacyLong, LegacyLong) { Bug("no SDL dialog session"); }
bool Dialog::_FGetCheckBox(LegacyLong) { Bug("no SDL dialog session"); return false; }
void Dialog::_InvertCheckBox(LegacyLong) { Bug("no SDL dialog session"); }
void Dialog::_SetCheckBox(LegacyLong, bool) { Bug("no SDL dialog session"); }
void Dialog::_GetEditText(LegacyLong, PString) { Bug("no SDL dialog session"); }
void Dialog::_SetEditText(LegacyLong, PString) { Bug("no SDL dialog session"); }
bool Dialog::_FAddToList(LegacyLong, PString) { Bug("no SDL dialog session"); return false; }
void Dialog::_ClearList(LegacyLong) { Bug("no SDL dialog session"); }
#endif
bool Dialog::_FDoSDL(LegacyLong focus, LegacyLong &result)
{
    result = ivNil;
    KauaiDialogs::Resource resource;
    KauaiDialogs::Session session;
    if (!_FReadResource(resource))
        return false;
    if (_tabs)
        resource.tabs = *_tabs;
    if (!session.Init(resource) || static_cast<LegacyLong>((session.bindings.size)()) != IvMac())
        return false;
    auto &host = KauaiSDL::Main();
    bool shown = false;
    try
    {
        _session = &session;
        session.focus = focus;
        session.context = this;
        session.changed = [](void *context, std::int32_t &item) {
            auto dialog = static_cast<PDialog>(context);
            LegacyLong idit = item;
            const bool close = dialog->_FDitChange(&idit);
            item = static_cast<std::int32_t>(idit);
            return close;
        };
        SetValues(0, IvMac());
        host.FlushInput(true, true);
        host.SuspendFocusEvents(true);
        shown = KauaiSDL::RunDialog(session, host.WindowID(), [](void *) { KauaiSDL::Main().Present(); });
        if (shown)
        {
            result = session.result;
            if (result != ivNil && !FGetValues(0, IvMac()))
                result = ivNil;
        }
    }
    catch (const std::exception &)
    {
        result = ivNil;
    }
    _session = nullptr;
    if (!KauaiSDL::CurrentDialog().active) host.ResumeAfterModal();
    return shown;
}
