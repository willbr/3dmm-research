/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_pack_runtime.h"
#include "studio_iso.h"
#include "utestres.h"
#include "platform_text.h"
#include "text_utf8.h"
#include "sdl_host.h"
#include "sdl_dialog.h"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_video.h>
#include <mutex>
#include <stdexcept>
#include "studio.h"
#include "studio_pack_options.h"
#undef size
namespace StudioPacks
{
std::string UTF8(PString text)
{
#if !defined(_WIN32) && !defined(UNICODE)
    return {text->Psz(), static_cast<std::size_t>(text->Cch())};
#else
#ifdef UNICODE
    const auto encoding = BigLittle(KauaiText::Encoding::Utf16BE, KauaiText::Encoding::Utf16LE);
#else
    const auto encoding = KauaiText::Encoding::WindowsAnsi;
#endif
    auto bytes = text->Cch() * sizeof(achar);
    auto count = KauaiText::ToUtf16(text->Psz(), bytes, encoding, nullptr, 0);
    std::vector<std::uint16_t> units(count);
    std::string result;
    if ((!bytes || count) && (!count || KauaiText::ToUtf16(text->Psz(), bytes, encoding, units.data(), count)))
        KauaiText::EncodeUtf8(units, result);
    return result;
#endif
}
bool SetText(PString text, const std::string &utf8)
{
#if !defined(_WIN32) && !defined(UNICODE)
    if (utf8.size() > 255 || utf8.find('\0') != std::string::npos)
        return false;
    text->SetSz(const_cast<char *>(utf8.c_str()));
    return true;
#else
    std::vector<std::uint16_t> units;
    if (!KauaiText::DecodeUtf8(utf8, units))
        return false;
#ifdef UNICODE
    if (units.size() > 255)
        return false;
    units.push_back(0);
    text->SetSz(units.data());
#else
    auto encoding = BigLittle(KauaiText::Encoding::Utf16BE, KauaiText::Encoding::Utf16LE);
    auto count = KauaiText::ToAnsi(units.data(), units.size() * 2, encoding, nullptr, 0);
    if (count > 255 || (!units.empty() && !count))
        return false;
    std::vector<char> bytes(count + 1, 0);
    if (count && !KauaiText::ToAnsi(units.data(), units.size() * 2, encoding, bytes.data(), count))
        return false;
    text->SetSz(bytes.data());
#endif
    return UTF8(text) == utf8;
#endif
}
TagSourceAccess ResolveSource(PString longName, PString shortName, PFilename path)
{
    std::string directory;
    auto access = Resolve(UTF8(longName), UTF8(shortName), directory);
    switch (access)
    {
    case Access::Available: {
        String native;
        return SetText(&native, directory) && path->FBuildFromPath(&native, kftgDir) ? TagSourceAccess::Available
                                                                                     : TagSourceAccess::Missing;
    }
    case Access::Disabled:
        return TagSourceAccess::Disabled;
    case Access::Missing:
        return TagSourceAccess::Missing;
    default:
        return TagSourceAccess::Legacy;
    }
}
namespace
{
void Alert(const std::string &message)
{
    KauaiDialogs::Session session;
    if (session.InitAlert(message, "Expansions", KauaiDialogs::AlertButtons::ok))
        KauaiSDL::RunDialog(session);
}
void Put(PDialog dialog, LegacyLong control, const std::string &text)
{
    String native;
    if (!SetText(&native, text))
        throw std::runtime_error("This path is too long or cannot be represented by the movie engine");
    auto item = dialog->IditFromSit(control);
    if (!dialog->FPutStn(item, &native))
        throw std::runtime_error("Cannot update expansion options");
    dialog->SetValues(item, item + 1);
}
std::filesystem::path Get(PDialog dialog, LegacyLong control)
{
    auto item = dialog->IditFromSit(control);
    if (!dialog->FGetValues(item, item + 1))
        throw std::runtime_error("Cannot read the selected path");
    String native;
    dialog->GetStn(item, &native);
    auto text = UTF8(&native);
    if (text.empty())
        throw std::runtime_error("Choose an installation source first");
    return std::filesystem::u8path(text);
}
struct Selection
{
    std::mutex mutex;
    bool waiting = false, ready = false;
    LegacyLong control = 0;
    std::string path, error;
};
void SDLCALL Chosen(void *context, const char *const *files, int)
{
    std::unique_ptr<std::shared_ptr<Selection>> owner(static_cast<std::shared_ptr<Selection> *>(context));
    auto state = *owner;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!files)
        state->error = SDL_GetError();
    else if (files[0])
        state->path = files[0];
    state->ready = true;
    state->waiting = false;
}
} // namespace
struct Options::Impl
{
    Edits edits;
    std::shared_ptr<Selection> selection = std::make_shared<Selection>();
    bool refreshed = false;
    void Refresh(PDialog dialog)
    {
        for (std::size_t i = 0; i < 3; ++i)
        {
            auto info = edits.Inspect(i);
            auto &preference = edits.values[i];
            auto &active = ActivePreferences()[i];
            std::string status = !info.installed      ? "Not installed"
                                 : preference.remove  ? "Removal queued (undo)"
                                 : !info.valid        ? "Files missing or changed"
                                 : preference.enabled ? "Enabled"
                                                      : "Disabled";
            if (info.valid && !edits.Managed(i))
                status += " (external)";
            else if (info.valid && !preference.remove &&
                     (active.generation != preference.generation || active.enabled != preference.enabled))
                status = "Next launch: " + std::string(preference.enabled ? "enabled" : "disabled");
            Put(dialog, IDC_PACK_STATUS_NICK + LegacyLong(i), status);
            auto item = dialog->IditFromSit(IDC_PACK_NICK + LegacyLong(i));
            dialog->PutCheck(item, preference.enabled && info.valid && !preference.remove);
            dialog->SetValues(item, item + 1);
            dialog->FEnableControl(IDC_PACK_NICK + LegacyLong(i), info.valid && !preference.remove);
            dialog->FEnableControl(IDC_PACK_INSTALL_NICK + LegacyLong(i), !edits.Managed(i));
            dialog->FEnableControl(IDC_PACK_REMOVE_NICK + LegacyLong(i), edits.Managed(i));
        }
        refreshed = true;
    }
    void Browse(PDialog dialog, LegacyLong control)
    {
        {
            std::lock_guard<std::mutex> lock(selection->mutex);
            if (selection->waiting)
                return;
            selection->waiting = true;
            selection->ready = false;
            selection->path.clear();
            selection->error.clear();
            selection->control = control == IDC_PACK_BASE_BROWSE     ? IDC_PACK_BASE
                                 : control == IDC_PACK_FIXUPS_BROWSE ? IDC_PACK_FIXUPS
                                                                     : IDC_PACK_SOURCE;
        }
        auto context = new std::shared_ptr<Selection>(selection);
        auto window = SDL_GetWindowFromID(KauaiSDL::Main().WindowID());
        static const SDL_DialogFileFilter iso[] = {{"Disc images", "iso"}}, zip[] = {{"Image fixups", "zip"}};
        if (control == IDC_PACK_FOLDER)
            SDL_ShowOpenFolderDialog(Chosen, context, window, nullptr, false);
        else
            SDL_ShowOpenFileDialog(Chosen, context, window, control == IDC_PACK_FIXUPS_BROWSE ? zip : iso, 1, nullptr,
                                   false);
    }
};
Options::Options() : impl(new Impl)
{
}
Options::~Options() = default;
void Options::Initialize(PDialog dialog)
{
    String base;
    if (SetText(&base, StudioContent::CurrentISO().image))
        dialog->FPutStn(dialog->IditFromSit(IDC_PACK_BASE), &base);
    for (std::size_t i = 0; i < 3; ++i)
        dialog->PutCheck(dialog->IditFromSit(IDC_PACK_NICK + LegacyLong(i)),
                         impl->edits.values[i].enabled && impl->edits.Inspect(i).valid);
}
bool Options::Handle(PDialog dialog, LegacyLong item, bool &handled)
{
    handled = false;
    try
    {
        if (item < 0)
        {
            if (!impl->refreshed)
                impl->Refresh(dialog);
            std::string path, error;
            LegacyLong control = 0;
            {
                std::lock_guard<std::mutex> lock(impl->selection->mutex);
                if (impl->selection->ready)
                {
                    path = impl->selection->path;
                    error = impl->selection->error;
                    control = impl->selection->control;
                    impl->selection->ready = false;
                }
            }
            if (!error.empty())
                Alert(error);
            if (!path.empty())
                Put(dialog, control, path);
            return false;
        }
        DialogItem binding;
        dialog->GetDit(item, &binding);
        auto control = binding.sitMin;
        if (control == 1)
        {
            handled = true;
            impl->edits.Commit();
            return true;
        }
        if (control >= IDC_PACK_NICK && control <= IDC_PACK_FRANKIE)
        {
            handled = true;
            if (dialog->FGetValues(item, item + 1))
                impl->edits.values[control - IDC_PACK_NICK].enabled = dialog->FGetCheck(item);
            impl->Refresh(dialog);
        }
        else if (control >= IDC_PACK_INSTALL_NICK && control <= IDC_PACK_INSTALL_FRANKIE)
        {
            handled = true;
            auto source = Get(dialog, IDC_PACK_SOURCE);
            std::filesystem::path base, fixups;
            if (control == IDC_PACK_INSTALL_NICK && !std::filesystem::is_directory(source))
            {
                base = Get(dialog, IDC_PACK_BASE);
                fixups = Get(dialog, IDC_PACK_FIXUPS);
            }
            impl->edits.Install(control - IDC_PACK_INSTALL_NICK, source, base, fixups);
            impl->Refresh(dialog);
        }
        else if (control >= IDC_PACK_REMOVE_NICK && control <= IDC_PACK_REMOVE_FRANKIE)
        {
            handled = true;
            impl->edits.ToggleRemove(control - IDC_PACK_REMOVE_NICK);
            impl->Refresh(dialog);
        }
        else if (control == IDC_PACK_FOLDER || control == IDC_PACK_ISO || control == IDC_PACK_BASE_BROWSE ||
                 control == IDC_PACK_FIXUPS_BROWSE)
        {
            handled = true;
            impl->Browse(dialog, control);
        }
    }
    catch (const std::exception &error)
    {
        handled = item >= 0;
        Alert(error.what());
    }
    return false;
}
} // namespace StudioPacks
