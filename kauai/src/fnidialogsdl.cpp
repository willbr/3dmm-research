/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// POSIX document dialogs use the same SDL browser as Studio's portfolio.
#include "sdl_file_dialog.h"
#include "sdl_dialog.h"
#include "sdl_host.h"
#include "platform_text.h"
#include "text_utf8.h"
#include <exception>
#include "util.h"
ASSERTNAME
namespace
{
bool Label(const achar *text, std::string &utf8)
{
    const auto bytes = CchSz(const_cast<achar *>(text)) * sizeof(achar);
#ifdef UNICODE
    const auto encoding = BigLittle(KauaiText::Encoding::Utf16BE, KauaiText::Encoding::Utf16LE);
#else
    const auto encoding = KauaiText::Encoding::WindowsAnsi;
#endif
    const auto count = KauaiText::ToUtf16(text, bytes, encoding, nullptr, 0);
    std::vector<std::uint16_t> units(count);
    return (!bytes || count) && (!count || KauaiText::ToUtf16(text, bytes, encoding, units.data(), count)) &&
           KauaiText::EncodeUtf8(units, utf8);
}

bool Choose(Filename *selection, achar *filters, CoreNative::WindowHandle owner, bool open)
{
    selection->SetNil();
    auto &host = KauaiSDL::Main();
    struct ResumeInput
    {
        ~ResumeInput()
        {
            if (!KauaiSDL::CurrentDialog().active)
                KauaiSDL::Main().ResumeAfterModal();
        }
    } resume;
    try
    {
        KauaiSDL::FileDialog dialog;
        auto &browser = dialog.browser;
        browser.open = open;
        dialog.title = open ? "Open" : "Save As";
        if (!browser.Navigate("."))
            return false;
        browser.home = browser.directory;
        // The legacy API supplies label/pattern pairs followed by an empty label.
        for (auto part = filters; part && *part;)
        {
            KauaiFiles::BrowserFilter filter;
            if (!Label(part, filter.label))
                return false;
            part += CchSz(part) + 1;
            if (!*part || !Label(part, filter.patterns))
                return false;
            part += CchSz(part) + 1;
            browser.filters.push_back(std::move(filter));
        }
        if (!browser.Refresh())
            return false;
        host.FlushInput(true, true);
        host.SuspendFocusEvents(true);
        const auto parent = host.IsWindow(owner) ? host.WindowID() : 0;
        for (;;)
        {
            if (!KauaiSDL::RunFileDialog(dialog, parent, [](void *) { KauaiSDL::Main().Present(); }) ||
                !dialog.accepted)
                return false;
            String path;
            std::basic_string<KauaiFiles::PathChar> native;
            if (!KauaiFiles::NativePathFromUtf8(browser.selection, native) || (native.size)() > kcchMaxStn)
                return false;
            path.SetRgch(reinterpret_cast<achar *>(&native[0]), static_cast<LegacyLong>((native.size)()));
            Filename candidate;
            if (!candidate.FBuildFromPath(&path))
                return false;
            if (!open)
            {
                const auto exists = candidate.TExists();
                if (exists == tMaybe)
                    return false;
                if (exists == tYes)
                {
                    KauaiDialogs::Session confirm;
                    if (!confirm.InitAlert("Replace the existing file?", "Save As",
                                           KauaiDialogs::AlertButtons::yesNo) ||
                        !KauaiSDL::RunDialog(confirm, parent, [](void *) { KauaiSDL::Main().Present(); }))
                        return false;
                    if (confirm.AlertAnswer() != KauaiDialogs::AlertResult::yes)
                        continue;
                }
            }
            *selection = candidate;
            return true;
        }
    }
    catch (const std::exception &)
    {
        return false;
    }
}
} // namespace

bool Filename::FGetOpen(achar *filters, CoreNative::WindowHandle owner)
{
    AssertThis(0);
    return Choose(this, filters, owner, true);
}

bool Filename::FGetSave(achar *filters, CoreNative::WindowHandle owner)
{
    AssertThis(0);
    return Choose(this, filters, owner, false);
}
