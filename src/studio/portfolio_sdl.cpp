/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_file_dialog.h"
#include "portfolio_icon.h"
#include "sdl_dialog.h"
#include "sdl_host.h"
#include "pixel_canvas.h"
#include "platform_text.h"
#include "text_utf8.h"
#include <SDL3/SDL_filesystem.h>
#include <exception>
#include <algorithm>
#include "studio.h"
#include "portfolio_host.h"
#include "portfolio_data.h"
ASSERTNAME
namespace StudioPortfolio
{
namespace
{
// NUL-separated UTF-8 paths stored as binary avoid the legacy ANSI string
// conversion. The INI backend retains all other settings and writes atomically.
constexpr std::size_t FavoritesCapacity = 65536;
std::vector<std::string> StoredFolders(PZString setting)
{
    std::vector<char> data(FavoritesCapacity);
    bool missing = false;
    if (!vapp.FGetSetRegKey(setting, data.data(), long((data.size)()), fregBinary, &missing) || missing)
        return {};
    std::vector<std::string> result;
    for (std::size_t start = 0; start < (data.size)() && data[start];)
    {
        const auto end = std::find(data.begin() + start, data.end(), '\0');
        if (end == data.end() || (result.size)() == 64)
            return {};
        std::string path(data.begin() + start, end);
        std::vector<std::uint16_t> units;
        if (!KauaiText::DecodeUtf8(path, units))
            return {};
        result.push_back(std::move(path));
        start = std::size_t(end - data.begin()) + 1;
    }
    return result;
}
bool SaveFolders(PZString setting, const std::vector<std::string> &folders)
{
    if ((folders.size)() > 64)
        return false;
    std::vector<char> data;
    for (const auto &folder : folders)
    {
        if (folder.empty() || folder.find('\0') != std::string::npos ||
            (folder.size)() >= FavoritesCapacity - (data.size)())
            return false;
        data.insert(data.end(), folder.begin(), folder.end());
        data.push_back(0);
    }
    if ((data.size)() == FavoritesCapacity)
        return false;
    data.push_back(0);
    return vapp.FGetSetRegKey(setting, data.data(), long((data.size)()), fregSetKey | fregBinary);
}
bool SaveFavoriteFolders(void *, const std::vector<std::string> &folders)
{
    return SaveFolders(PszLit("FavoriteFolders"), folders);
}
std::string Utf8(const achar *text)
{
    if (!text)
        return {};
    const auto bytes = CchSz(const_cast<achar *>(text)) * sizeof(achar);
#ifdef UNICODE
    const auto encoding = BigLittle(KauaiText::Encoding::Utf16BE, KauaiText::Encoding::Utf16LE);
#else
    const auto encoding = KauaiText::Encoding::WindowsAnsi;
#endif
    const auto count = KauaiText::ToUtf16(text, bytes, encoding, nullptr, 0);
    std::vector<std::uint16_t> units(count);
    std::string result;
    if ((bytes && !count) || (count && !KauaiText::ToUtf16(text, bytes, encoding, units.data(), count)) ||
        !KauaiText::EncodeUtf8(units, result))
        return {};
    return result;
}
bool Legacy(const std::string &text, String &result)
{
    std::vector<std::uint16_t> units;
    if (!KauaiText::DecodeUtf8(text, units))
        return false;
#ifdef UNICODE
    if ((units.size)() > kcchMaxStn)
        return false;
    result.SetRgch(reinterpret_cast<achar *>(units.data()), static_cast<LegacyLong>((units.size)()));
#else
    const auto encoding = BigLittle(KauaiText::Encoding::Utf16BE, KauaiText::Encoding::Utf16LE);
    const auto count = KauaiText::ToAnsi(units.data(), (units.size)() * 2, encoding, nullptr, 0);
    if (count > kcchMaxStn || (!(units.empty)() && !count))
        return false;
    std::vector<char> bytes(count);
    if (count && !KauaiText::ToAnsi(units.data(), (units.size)() * 2, encoding, bytes.data(), count))
        return false;
    result.SetRgch(bytes.data(), static_cast<LegacyLong>(count));
#endif
    return true;
}
std::string MovieDirectory(const std::string &profile)
{
    // Resolve the OS's Documents folder, including redirection/OneDrive on
    // Windows. Profile data and bundled samples keep their existing locations.
    const auto documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    if (!documents)
        return profile;
    const std::string directory = std::string(documents) + "3DMMForever";
    String legacy;
    std::basic_string<KauaiFiles::PathChar> native;
    if (!Legacy(directory, legacy) || Utf8(legacy.Psz()) != directory ||
        !KauaiFiles::NativePathFromUtf8(directory, native) || !KauaiFiles::MakeDirectories(native.c_str()))
        return profile;
    return directory;
}
struct Preview
{
    LegacyUlong flags;
    std::vector<Color> palette;
    bool Image(PMaskedBitmapMBMP bitmap, KauaiSDL::DialogImage &image, int width = 0, int height = 0)
    {
        if (!bitmap || (palette.empty)())
            return false;
        RC bounds;
        bitmap->GetRc(&bounds);
        image.width = width ? width : bounds.Dxp();
        image.height = height ? height : bounds.Dyp();
        if (image.width <= 0 || image.height <= 0 || image.width > 4096 || image.height > 4096)
            return false;
        image.pixels.assign(static_cast<std::size_t>(image.width) * image.height, 0xFF000000);
        RC rectangle(0, 0, image.width, image.height);
        if (!bitmap->FDraw32({image.pixels.data(), image.width, image.height, image.width * 4}, rectangle,
                             palette.data(), static_cast<LegacyLong>((palette.size)())))
            return false;
        for (auto &pixel : image.pixels)
            pixel |= 0xFF000000;
        return true;
    }
    void Asset(ChunkNumber number, KauaiSDL::DialogImage &image)
    {
        auto bitmap =
            static_cast<PMaskedBitmapMBMP>(vapp.PcrmAll()->PbacoFetch(kctgMbmp, number, MaskedBitmapMBMP::FReadMbmp));
        if (bitmap)
        {
            Image(bitmap, image);
            ReleasePpo(&bitmap);
        }
    }
    static bool Load(void *context, const std::string &path, KauaiSDL::DialogImage &image)
    {
        auto &preview = *static_cast<Preview *>(context);
        String legacy;
        if (!Legacy(path, legacy))
            return false;
        auto bitmap = LoadPreviewPath(&legacy, preview.flags);
        if (!bitmap)
            return false;
        const bool result = preview.Image(bitmap, image, 144, 80);
        ReleasePpo(&bitmap);
        return result;
    }
};
} // namespace
bool Choose(Filename *selection, const Request &request)
{
    if (!request.open)
        selection->SetNil();
    auto &host = KauaiSDL::Main();
    bool accepted = false;
    try
    {
        KauaiSDL::FileDialog dialog;
        auto &browser = dialog.browser;
        browser.open = request.open;
        browser.defaultExtension = Utf8(request.defaultExtension);
        const bool moviePortfolio = (request.previews & fpfPortPrevMovie) != 0;
        dialog.mediaPlaces = !moviePortfolio;
        dialog.title = Utf8(request.title);
        Filename userDirectory;
        String path;
        vapp.GetFniUser(&userDirectory);
        userDirectory.GetStnPath(&path);
        browser.home = MovieDirectory(Utf8(path.Psz()));
        std::string initial = browser.home;
        if (request.open && request.initialDirectory)
        {
            request.initialDirectory->GetStnPath(&path);
            initial = Utf8(path.Psz());
        }
        if (request.open && moviePortfolio)
        {
            const auto previous = StoredFolders(PszLit("LastMovieFolder"));
            if (!previous.empty())
                browser.Navigate(previous.front());
        }
        if (browser.directory.empty() && !browser.Navigate(initial) && !browser.Navigate(browser.home))
            return false;
        if (!request.open && request.defaultName)
            browser.name = Utf8(request.defaultName->Psz());
        // Original label/pattern pairs are double-NUL terminated.
        for (auto filter = request.filter; filter && *filter;)
        {
            auto label = Utf8(filter);
            filter += CchSz(filter) + 1;
            auto pattern = Utf8(filter);
            filter += CchSz(filter) + 1;
            browser.filters.push_back({std::move(label), std::move(pattern)});
        }
        browser.Refresh();
        dialog.favorites = StoredFolders(PszLit("FavoriteFolders"));
        dialog.saveFavorites = SaveFavoriteFolders;
        Preview preview{request.previews};
        auto palette = GraphicsPort::PglclrGetPalette();
        if (palette)
        {
            for (LegacyLong i = 0; i < palette->IvMac(); ++i)
            {
                Color color;
                palette->Get(i, &color);
                preview.palette.push_back(color);
            }
            ReleasePpo(&palette);
        }
        preview.Asset(request.open ? kcnoMbmpPortBackOpen : kcnoMbmpPortBackSave, dialog.artwork);
        preview.Asset(kcnoMbmpPortBtnOk, dialog.acceptButton);
        preview.Asset(kcnoMbmpPortBtnCancel, dialog.cancelButton);
        preview.Asset(kcnoMbmpPortBtnHome, dialog.homeButton);
        preview.Asset(kcnoMbmpPortBtnOkSel, dialog.acceptPressed);
        preview.Asset(kcnoMbmpPortBtnCancelSel, dialog.cancelPressed);
        preview.Asset(kcnoMbmpPortBtnHomeSel, dialog.homePressed);
        DocumentIcon(dialog.documentIcon);
        dialog.preview = Preview::Load;
        dialog.context = &preview;
        do
        {
            if (request.sound != cnoNil)
            {
                auto resource = vapp.PcrmAll()->PcrfFindChunk(kctgWave, request.sound);
                if (resource)
                    vpsndm->SiiPlay(resource, kctgWave, request.sound);
            }
            host.FlushInput(true, true);
            host.SuspendFocusEvents(true);
            const bool shown = KauaiSDL::RunFileDialog(dialog, host.WindowID());
            if (shown && request.open && moviePortfolio && !SaveFolders(PszLit("LastMovieFolder"), {browser.directory}))
                std::fprintf(stderr, "Could not remember the last movie folder.\n");
            if (!KauaiSDL::CurrentDialog().active)
            {
                host.ResumeAfterModal();
                host.Present(true);
            }
            if (!shown)
                PushErc(ercSocPortfolioFailed);
            if (!shown || !dialog.accepted)
                break;
            String chosen;
            Filename candidate;
            if (!Legacy(browser.selection, chosen) || !candidate.FBuildFromPath(&chosen, 0))
            {
                browser.error = "This file name is too long or unavailable.";
                continue;
            }
            if (!request.open)
            {
                const auto exists = candidate.TExists();
                if (exists == tMaybe)
                {
                    vapp.DisplayErrors();
                    continue;
                }
                if (exists == tYes)
                {
                    String leaf, message;
                    candidate.GetLeaf(&leaf);
                    RemoveSaveExtension(&leaf);
                    if (!vapp.FGetStnApp(idsReplaceFile, &message) ||
                        vapp.TModal(vapp.PcrmAll(), ktpcQueryOverwrite, &message, bkYesNo, kstidQueryOverwrite,
                                    &leaf) != tYes)
                        continue;
                }
            }
            *selection = candidate;
            accepted = true;
            break;
        } while (true);
    }
    catch (const std::exception &)
    {
        PushErc(ercSocPortfolioFailed);
    }
    if (!KauaiSDL::CurrentDialog().active)
        host.ResumeAfterModal();
    if (!accepted && !request.open)
        selection->SetNil();
    return accepted;
}
} // namespace StudioPortfolio
