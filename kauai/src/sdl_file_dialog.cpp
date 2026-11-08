/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_file_dialog.h"
#include "sdl_modal_internal.h"
#include "sdl_classic_dialog.h"
#include "sdl_dialog_font.h"
#include <SDL3/SDL.h>
#include "imgui_stdlib.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <numeric>
namespace KauaiSDL
{
namespace
{
namespace fs = std::filesystem;
SDL_Texture *Texture(SDL_Renderer *renderer, const DialogImage &image)
{
    if (image.width <= 0 || image.height <= 0 || image.pixels.size() != std::size_t(image.width) * image.height)
        return nullptr;
    auto texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
    if (texture && !SDL_UpdateTexture(texture, nullptr, image.pixels.data(), image.width * 4))
    {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    if (texture)
    {
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    return texture;
}
ImTextureID ImageID(SDL_Texture *texture)
{
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
}
std::string Leaf(const std::string &path)
{
    const auto native = fs::u8path(path).lexically_normal();
    const auto name = native.filename().u8string();
    return name.empty() ? native.root_path().u8string() : name;
}
void FolderIcon(ImDrawList *draw, ImVec2 p, float scale)
{
    draw->AddRectFilled(ImVec2(p.x, p.y + 2 * scale), ImVec2(p.x + 6 * scale, p.y + 5 * scale),
                        IM_COL32(173, 134, 33, 255));
    draw->AddRectFilled(ImVec2(p.x, p.y + 4 * scale), ImVec2(p.x + 14 * scale, p.y + 12 * scale),
                        IM_COL32(173, 134, 33, 255));
    draw->AddRectFilled(ImVec2(p.x + scale, p.y + 4 * scale), ImVec2(p.x + 13 * scale, p.y + 11 * scale),
                        IM_COL32(255, 228, 123, 255));
    draw->AddLine(ImVec2(p.x + scale, p.y + 4 * scale), ImVec2(p.x + 13 * scale, p.y + 4 * scale), ClassicDialog::White,
                  scale);
}
void FileIcon(ImDrawList *draw, ImVec2 p, float scale)
{
    draw->AddRectFilled(p, ImVec2(p.x + 11 * scale, p.y + 14 * scale), ClassicDialog::White);
    draw->AddRect(p, ImVec2(p.x + 11 * scale, p.y + 14 * scale), ClassicDialog::Shadow);
    draw->AddRectFilled(ImVec2(p.x + 2 * scale, p.y + 3 * scale), ImVec2(p.x + 9 * scale, p.y + 10 * scale),
                        ClassicDialog::Navy);
    draw->AddTriangleFilled(ImVec2(p.x + 4 * scale, p.y + 4 * scale), ImVec2(p.x + 4 * scale, p.y + 9 * scale),
                            ImVec2(p.x + 8 * scale, p.y + 6.5f * scale), IM_COL32(0, 255, 0, 255));
}
struct Place
{
    std::string label, path;
    int depth = 0;
};
struct FileDate
{
    std::string text;
    std::int64_t order = 0;
};
struct View
{
    FileDialog &dialog;
    SDL_Texture *artwork = nullptr, *accept = nullptr, *cancel = nullptr, *home = nullptr, *preview = nullptr;
    SDL_Texture *acceptPressed = nullptr, *cancelPressed = nullptr, *homePressed = nullptr;
    SDL_Texture *documentIcon = nullptr;
    ImFont *titleFont = nullptr;
    SDL_FRect backdropBounds{};
    bool initialized = false, firstFrame = true, details = true, popupWasOpen = false;
    std::string path, previewed, listedDirectory;
    std::vector<std::string> history;
    std::vector<Place> places;
    std::vector<FileDate> dates;
    std::vector<std::size_t> order;
    const KauaiFiles::BrowserEntry *listedEntries = nullptr;
    void Fonts()
    {
        // The original portfolio paints its title with Windows' System font.
        titleFont = DialogFont::Add("System", false, 16);
        if (!titleFont)
            titleFont = DialogFont::Add("DejaVu Sans", true, 16);
    }
    void Release()
    {
        for (auto texture :
             {artwork, accept, cancel, home, preview, acceptPressed, cancelPressed, homePressed, documentIcon})
            SDL_DestroyTexture(texture);
    }
    void Background(SDL_Renderer *renderer)
    {
        // SDL's software triangle rasterizer can overflow for large ImGui
        // images. Its image blitter handles the full-size backdrop directly.
        if (artwork)
            SDL_RenderTexture(renderer, artwork, nullptr, &backdropBounds);
    }
    bool Navigate(const std::string &directory, bool remember = true)
    {
        auto &browser = dialog.browser;
        if (!browser.Navigate(directory))
            return false;
        path = Leaf(browser.directory);
        if (remember && (history.empty() || history.back() != browser.directory))
            history.push_back(browser.directory);
        return true;
    }
    void Places()
    {
        places.clear();
        const auto add = [&](const char *label, const char *directory) {
            std::error_code error;
            if (directory && fs::is_directory(fs::u8path(directory), error))
                places.push_back({label, directory});
        };
        add("Home", SDL_GetUserFolder(SDL_FOLDER_HOME));
        add("Desktop", SDL_GetUserFolder(SDL_FOLDER_DESKTOP));
        add("Documents", SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS));
        add("Downloads", SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS));
        add("Pictures", SDL_GetUserFolder(SDL_FOLDER_PICTURES));
        add("Music", SDL_GetUserFolder(SDL_FOLDER_MUSIC));
        add("Videos", SDL_GetUserFolder(SDL_FOLDER_VIDEOS));
        add("My movies", dialog.browser.home.c_str());
        places.push_back({"This computer", {}});
        std::vector<fs::path> ancestors;
        auto current = fs::u8path(dialog.browser.directory).lexically_normal();
        for (auto ancestor = current; !ancestor.empty();)
        {
            ancestors.push_back(ancestor);
            auto parent = ancestor.parent_path();
            if (parent == ancestor)
                break;
            ancestor = parent;
        }
        std::reverse(ancestors.begin(), ancestors.end());
        const auto roots = KauaiFiles::RootMask();
        for (unsigned index = 0; index < 32; ++index)
        {
            KauaiFiles::PathChar buffer[16] = {};
            if (!(roots & (std::uint32_t(1) << index)) || !KauaiFiles::RootPath(index, buffer, 16))
                continue;
            // RootPath returns only an ASCII drive prefix or '/'.
            std::string root;
            for (const auto *p = buffer; *p; ++p)
                root += static_cast<char>(*p);
            auto rootPath = fs::u8path(root).lexically_normal();
            places.push_back({root, root, 1});
            if (rootPath == current.root_path())
                for (std::size_t n = 1; n < ancestors.size(); ++n)
                    places.push_back({ancestors[n].filename().u8string(), ancestors[n].u8string(), int(n + 1)});
        }
    }
    bool Dates()
    {
        const auto &browser = dialog.browser;
        if (listedDirectory == browser.directory && listedEntries == browser.entries.data() &&
            dates.size() == browser.entries.size())
            return false;
        listedDirectory = browser.directory;
        listedEntries = browser.entries.data();
        dates.assign(browser.entries.size(), {});
        order.resize(dates.size());
        std::iota(order.begin(), order.end(), 0);
        for (std::size_t n = 0; n < dates.size(); ++n)
        {
            std::error_code error;
            auto stamp = fs::last_write_time(fs::u8path(browser.entries[n].path), error);
            if (error)
                continue;
            dates[n].order = stamp.time_since_epoch().count();
            auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                stamp - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            auto value = std::chrono::system_clock::to_time_t(system);
            if (auto local = std::localtime(&value))
            {
                char text[32];
                if (std::strftime(text, sizeof(text), "%d/%m/%Y %H:%M", local))
                    dates[n].text = text;
            }
        }
        return true;
    }
    void Draw(KauaiDialogs::Session &session, float scale, DialogState &state, SDL_Renderer *renderer)
    {
        auto &browser = dialog.browser;
        if (!initialized)
        {
            artwork = Texture(renderer, dialog.artwork);
            accept = Texture(renderer, dialog.acceptButton);
            cancel = Texture(renderer, dialog.cancelButton);
            home = Texture(renderer, dialog.homeButton);
            acceptPressed = Texture(renderer, dialog.acceptPressed);
            cancelPressed = Texture(renderer, dialog.cancelPressed);
            homePressed = Texture(renderer, dialog.homePressed);
            documentIcon = Texture(renderer, dialog.documentIcon);
            path = Leaf(browser.directory);
            history.push_back(browser.directory);
            initialized = true;
        }
        if (previewed != browser.selection)
        {
            previewed = browser.selection;
            SDL_DestroyTexture(preview);
            preview = nullptr;
            DialogImage image;
            if (!previewed.empty() && dialog.preview && dialog.preview(dialog.context, previewed, image))
                preview = Texture(renderer, image);
        }
        const auto viewport = ImGui::GetMainViewport();
        const ImVec2 size = state.embedded ? ImVec2(640 * scale, 480 * scale) : viewport->Size;
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + (viewport->Size.x - size.x) / 2,
                                       viewport->Pos.y + (viewport->Size.y - size.y) / 2));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("##portfolio", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         (artwork ? ImGuiWindowFlags_NoBackground : 0) |
                         (state.embedded ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                                         : ImGuiWindowFlags_HorizontalScrollbar));
        const auto window = ImGui::GetWindowPos();
        const ImVec2 origin(window.x - ImGui::GetScrollX(), window.y - ImGui::GetScrollY());
        const auto point = [&](float x, float y) { return ImVec2(origin.x + x * scale, origin.y + y * scale); };
        const auto pos = [&](float x, float y) { ImGui::SetCursorScreenPos(point(x, y)); };
        auto draw = ImGui::GetWindowDrawList();
        const auto image = [&](SDL_Texture *texture, float x, float y, float w, float h) {
            if (texture)
                draw->AddImage(ImageID(texture), point(x, y), point(x + w, y + h));
        };
        backdropBounds = {origin.x, origin.y, 640 * scale, 480 * scale};
        DialogControl backdrop;
        backdrop.id = 20;
        backdrop.label = "Portfolio artwork";
        backdrop.number = artwork ? 1 : 0;
        state.controls.push_back(backdrop);
        const auto describe = [&](unsigned id, const std::string &label,
                                  KauaiDialogs::ItemKind kind = KauaiDialogs::ItemKind::button,
                                  const std::string &text = std::string()) {
            DialogControl control;
            control.id = id;
            control.item = static_cast<int>(id);
            control.kind = kind;
            control.label = label;
            control.text = text;
            auto low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
            control.x = low.x - viewport->Pos.x;
            control.y = low.y - viewport->Pos.y;
            control.width = high.x - low.x;
            control.height = high.y - low.y;
            state.controls.push_back(std::move(control));
        };
        const auto button = [&](unsigned id, const char *label, float x, float y, float w, float h,
                                SDL_Texture *texture = nullptr, SDL_Texture *pressed = nullptr) {
            if (texture)
                SDL_GetTextureSize(texture, &w, &h);
            pos(x, y);
            ImGui::PushID(int(id));
            const bool clicked =
                ImGui::InvisibleButton(label, ImVec2(w * scale, h * scale), ImGuiButtonFlags_EnableNav);
            if (texture)
            {
                image(pressed && ImGui::IsItemActive() ? pressed : texture, x, y, w, h);
                if (ImGui::IsItemFocused())
                    ClassicDialog::Focus(draw, point(x + 4, y + 4), point(x + w - 4, y + h - 4), scale);
            }
            else
            {
                if (id < 4 || id == 13 || ImGui::IsItemHovered() || ImGui::IsItemActive())
                    ClassicDialog::Button(draw, point(x, y), point(x + w, y + h), ImGui::IsItemActive(),
                                          ImGui::IsItemFocused(), false, scale);
                if (id < 4)
                {
                    const auto size = ImGui::CalcTextSize(label);
                    draw->AddText(
                        ImVec2(point(x + w / 2, y + h / 2).x - size.x / 2, point(x + w / 2, y + h / 2).y - size.y / 2),
                        ClassicDialog::Black, label);
                }
            }
            describe(id, label, id == 12 || id == 13 ? KauaiDialogs::ItemKind::combo : KauaiDialogs::ItemKind::button);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", label);
            ImGui::PopID();
            return clicked;
        };
        pos(12, 2);
        // ClassicDialog::Style already applies the fitted scale to fonts.
        ImGui::PushFont(titleFont);
        ImGui::TextUnformatted(dialog.title.c_str());
        ImGui::PopFont();

        // Explorer's compact white pane sits above the preview, leaving the
        // original 640x480 portfolio artwork and preview frame at their native positions.
        draw->AddRectFilled(point(8, 24), point(416, 192), ClassicDialog::White);
        ClassicDialog::Bevel(draw, point(8, 24), point(416, 192), true, scale);
        draw->AddRectFilled(point(10, 26), point(414, 53), IM_COL32(240, 240, 240, 255));
        pos(12, 31);
        ImGui::TextUnformatted(browser.open ? "Look in:" : "Save in:");
        draw->AddRectFilled(point(58, 27), point(277, 49), ClassicDialog::White);
        ClassicDialog::Bevel(draw, point(58, 27), point(277, 49), true, scale);
        FolderIcon(draw, point(61, 30), scale);
        pos(79, 30);
        ImGui::SetNextItemWidth(175 * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        const bool pathEntered = ImGui::InputText(
            "##folder", &path, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (browser.open && ImGui::IsItemEdited())
            browser.name = path;
        describe(10, "Folder", KauaiDialogs::ItemKind::editText, path);
        // In Open mode typed file paths use the location field. Retain the
        // original automation binding without adding a visible filename row.
        if (browser.open)
            describe(11, "File name", KauaiDialogs::ItemKind::editText, browser.name);
        if (ImGui::IsKeyPressed(ImGuiKey_L) && (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper))
            ImGui::SetKeyboardFocusHere(-1);
        ImGui::PopStyleVar();
        bool choose = false;
        if (pathEntered && !Navigate(path))
        {
            if (browser.open)
            {
                browser.name = path;
                choose = true;
            }
        }
        if (button(13, "Folders", 258, 28, 17, 19))
        {
            Places();
            ImGui::OpenPopup("##locations");
        }
        draw->AddTriangleFilled(point(263, 36), point(270, 36), point(266.5f, 40), ClassicDialog::Black);
        if (ImGui::IsPopupOpen("##locations"))
        {
            ImGui::SetNextWindowPos(point(58, 49));
            ImGui::SetNextWindowSizeConstraints(ImVec2(219 * scale, 0), ImVec2(350 * scale, 390 * scale));
        }
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(ClassicDialog::Navy));
        if (ImGui::BeginPopup("##locations"))
        {
            for (std::size_t n = 0; n < places.size(); ++n)
            {
                const auto &place = places[n];
                if (place.path.empty())
                {
                    ImGui::Separator();
                    ImGui::TextDisabled("%s", place.label.c_str());
                    continue;
                }
                ImGui::PushID(int(n));
                auto low = ImGui::GetCursorScreenPos();
                const bool selected =
                    fs::u8path(place.path).lexically_normal() == fs::u8path(browser.directory).lexically_normal();
                if (selected)
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(ClassicDialog::Navy));
                if (ImGui::Selectable("##place", selected, 0, ImVec2(0, 17 * scale)))
                {
                    Navigate(place.path);
                    ImGui::CloseCurrentPopup();
                }
                describe(0x100000 + unsigned(n), place.label, KauaiDialogs::ItemKind::button, place.path);
                auto popupDraw = ImGui::GetWindowDrawList();
                low.x += (2 + place.depth * 12) * scale;
                FolderIcon(popupDraw, low, scale);
                popupDraw->AddText(ImVec2(low.x + 19 * scale, low.y + scale),
                                   selected ? ClassicDialog::White : ClassicDialog::Black, place.label.c_str());
                if (selected)
                    ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);

        ImGui::BeginDisabled(history.size() < 2);
        if (button(5, "Back", 283, 27, 23, 22) && history.size() > 1)
        {
            history.pop_back();
            Navigate(history.back(), false);
        }
        const auto backColor = history.size() > 1 ? ClassicDialog::Black : ClassicDialog::Shadow;
        draw->AddTriangleFilled(point(288, 38), point(295, 32), point(295, 44), backColor);
        draw->AddRectFilled(point(294, 36), point(301, 40), backColor);
        state.controls.back().enabled = history.size() > 1;
        ImGui::EndDisabled();
        if (button(4, "Up", 308, 27, 23, 22))
            Navigate(fs::u8path(browser.directory).parent_path().u8string());
        FolderIcon(draw, point(312, 32), scale);
        draw->AddTriangleFilled(point(317, 31), point(313, 36), point(321, 36), ClassicDialog::Black);
        if (button(6, "Refresh", 333, 27, 23, 22))
            browser.Refresh();
        FolderIcon(draw, point(337, 32), scale);
        draw->AddLine(point(343, 30), point(348, 34), ClassicDialog::Navy, 2 * scale);
        if (button(12, "View and file types", 358, 27, 30, 22))
            ImGui::OpenPopup("##view-options");
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x)
                draw->AddRectFilled(point(363 + x * 4, 32 + y * 4), point(366 + x * 4, 35 + y * 4),
                                    ClassicDialog::Navy);
        draw->AddTriangleFilled(point(379, 36), point(385, 36), point(382, 40), ClassicDialog::Black);
        if (ImGui::BeginPopup("##view-options"))
        {
            if (ImGui::MenuItem("Details", nullptr, details))
                details = true;
            if (ImGui::MenuItem("List", nullptr, !details))
                details = false;
            ImGui::Separator();
            for (unsigned i = 0; i < browser.filters.size(); ++i)
                if (ImGui::MenuItem(browser.filters[i].label.c_str(), nullptr, browser.filter == i))
                {
                    browser.filter = i;
                    browser.Refresh();
                }
            ImGui::EndPopup();
        }

        const bool directoryChanged = listedDirectory != browser.directory;
        const bool changed = Dates();
        std::string navigate;
        pos(10, 55);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(.9f, .93f, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(ClassicDialog::Navy));
        if (directoryChanged)
            ImGui::SetNextWindowScroll(ImVec2(0, 0));
        if (ImGui::BeginTable("##files", details ? 2 : 1,
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_NoSavedSettings,
                              ImVec2(404 * scale, 134 * scale)))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort,
                                    266 * scale);
            if (details)
                ImGui::TableSetupColumn("Date modified", ImGuiTableColumnFlags_WidthFixed, 135 * scale);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            if (auto sorting = ImGui::TableGetSortSpecs())
                if (sorting->SpecsDirty || changed)
                {
                    const bool date = sorting->SpecsCount && sorting->Specs[0].ColumnIndex == 1;
                    const bool reverse =
                        sorting->SpecsCount && sorting->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                        if (browser.entries[a].directory != browser.entries[b].directory)
                            return browser.entries[a].directory;
                        if (date && dates[a].order != dates[b].order)
                            return reverse ? dates[a].order > dates[b].order : dates[a].order < dates[b].order;
                        return reverse ? a > b : a < b;
                    });
                    sorting->SpecsDirty = false;
                }
            ImGuiListClipper clip;
            clip.Begin(int(order.size()));
            while (clip.Step())
                for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row)
                {
                    auto i = order[row];
                    const auto &entry = browser.entries[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(int(i));
                    const auto low = ImGui::GetCursorScreenPos();
                    const auto columnWidth = ImGui::GetContentRegionAvail().x;
                    const bool selected = browser.selection == entry.path || browser.name == entry.name;
                    if (selected)
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                              ImGui::ColorConvertU32ToFloat4(ClassicDialog::Navy));
                    if (ImGui::Selectable("##entry", selected,
                                          ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns,
                                          ImVec2(0, 17 * scale)))
                    {
                        browser.Select(unsigned(i));
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            if (entry.directory)
                                navigate = entry.path;
                            else
                                choose = true;
                        }
                    }
                    describe(1000 + unsigned(i), entry.name);
                    if (selected)
                        ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("%s", entry.path.c_str());
                    auto listDraw = ImGui::GetWindowDrawList();
                    if (entry.directory)
                        FolderIcon(listDraw, ImVec2(low.x + 2 * scale, low.y), scale);
                    else
                    {
                        auto extension = fs::u8path(entry.name).extension().u8string();
                        for (auto &c : extension)
                            if (c >= 'A' && c <= 'Z')
                                c += 'a' - 'A';
                        if (documentIcon && extension == ".3mm")
                            listDraw->AddImage(ImageID(documentIcon), ImVec2(low.x + 2 * scale, low.y),
                                               ImVec2(low.x + 18 * scale, low.y + 16 * scale));
                        else
                            FileIcon(listDraw, ImVec2(low.x + 3 * scale, low.y), scale);
                    }
                    listDraw->PushClipRect(low, ImVec2(low.x + columnWidth - 4 * scale, low.y + 18 * scale), true);
                    listDraw->AddText(ImVec2(low.x + 21 * scale, low.y + scale),
                                      selected ? ClassicDialog::White : ClassicDialog::Navy, entry.name.c_str());
                    listDraw->PopClipRect();
                    if (details)
                    {
                        ImGui::TableSetColumnIndex(1);
                        ImGui::PushStyleColor(
                            ImGuiCol_Text,
                            ImGui::ColorConvertU32ToFloat4(selected ? ClassicDialog::White : ClassicDialog::Navy));
                        ImGui::TextUnformatted(dates[i].text.c_str());
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                }
            if (browser.entries.empty())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("No matching files.");
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(4);
        if (!navigate.empty())
            Navigate(navigate);

        // These are the original preview/button positions, including the
        // native portfolio's 3/4-button gap and 5/4-button spacing.
        image(preview, 56, 242, 144, 80);
        DialogControl thumbnail;
        thumbnail.id = 21;
        thumbnail.label = "Preview";
        thumbnail.number = preview ? 1 : 0;
        thumbnail.text = previewed;
        state.controls.push_back(thumbnail);
        if (!browser.open)
        {
            draw->AddRectFilled(point(8, 202), point(80, 223), IM_COL32(240, 240, 240, 255));
            pos(10, 206);
            ImGui::TextUnformatted("File name:");
            pos(82, 205);
            ImGui::SetNextItemWidth(234 * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3 * scale, 2 * scale));
            if (firstFrame)
                ImGui::SetKeyboardFocusHere();
            choose |= ImGui::InputText("##filename", &browser.name, ImGuiInputTextFlags_EnterReturnsTrue);
            describe(11, "File name", KauaiDialogs::ItemKind::editText, browser.name);
            ClassicDialog::Bevel(draw, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true, scale);
            ImGui::PopStyleVar();
        }
        int buttonX = 200 + 3 * (dialog.homeButton.width ? dialog.homeButton.width : 44) / 4;
        const auto buttonY = [](int height) { return 282 - (height ? height : 43) / 2; };
        if (button(3, "Home", float(buttonX), float(buttonY(dialog.homeButton.height)), 44, 43, home, homePressed))
            Navigate(browser.home);
        buttonX += 5 * (dialog.cancelButton.width ? dialog.cancelButton.width : 44) / 4;
        if (button(2, "Cancel", float(buttonX), float(buttonY(dialog.cancelButton.height)), 44, 43, cancel,
                   cancelPressed))
            session.Cancel();
        buttonX += 5 * (dialog.acceptButton.width ? dialog.acceptButton.width : 44) / 4;
        choose |= button(1, browser.open ? "Open" : "Save", float(buttonX), float(buttonY(dialog.acceptButton.height)),
                         44, 43, accept, acceptPressed);
        if (!popupWasOpen && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                session.Cancel();
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive() && !pathEntered)
                choose = true;
        }
        if (choose)
        {
            const auto previous = browser.directory;
            if (browser.Accept())
            {
                dialog.accepted = true;
                session.closed = true;
                session.result = 1;
            }
            if (previous != browser.directory)
            {
                path = Leaf(browser.directory);
                history.push_back(browser.directory);
            }
        }
        if (!browser.error.empty())
        {
            draw->AddRectFilled(point(8, 378), point(416, 408), ClassicDialog::Face);
            pos(12, 382);
            ImGui::PushTextWrapPos(410 * scale);
            ImGui::TextWrapped("%s", browser.error.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(640 * scale, 480 * scale));
        ImGui::End();
        popupWasOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        firstFrame = false;
    }
};
} // namespace
bool RunFileDialog(FileDialog &dialog, std::uint32_t parent, void (*idle)(void *), void *context)
{
    dialog.accepted = false;
    KauaiDialogs::Session session;
    if (!session.InitAlert("", dialog.title, KauaiDialogs::AlertButtons::okCancel))
        return false;
    session.resource.bounds = {0, 0, 320, 240};
    View view{dialog};
    ModalView modal{&view,
                    [](void *context, KauaiDialogs::Session &session, float scale, DialogState &state,
                       SDL_Renderer *renderer) { static_cast<View *>(context)->Draw(session, scale, state, renderer); },
                    [](void *context) { static_cast<View *>(context)->Release(); },
                    [](void *context) { static_cast<View *>(context)->Fonts(); },
                    parent != 0,
                    [](void *context, SDL_Renderer *renderer) { static_cast<View *>(context)->Background(renderer); }};
    return RunModal(session, parent, idle, context, &modal);
}
} // namespace KauaiSDL
