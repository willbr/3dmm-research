/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <exception>
#include "sdl_dialog.h"
#include "sdl_host.h"
#include "dlg.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The SDL dialog controller must not import the Windows SDK
#endif
bool Dialog::_FDoSDL(LegacyLong focus, LegacyLong &result)
{
    result = ivNil;
    KauaiDialogs::Resource resource;
    KauaiDialogs::Session session;
    if (!_FReadResource(resource) || !session.Init(resource) ||
        static_cast<LegacyLong>((session.bindings.size)()) != IvMac())
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
