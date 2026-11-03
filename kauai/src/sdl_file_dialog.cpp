/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_file_dialog.h"
#include "sdl_modal_internal.h"
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_stdlib.h"
#include <algorithm>
namespace KauaiSDL
{
namespace
{
SDL_Texture *Texture(SDL_Renderer *renderer, const DialogImage &image)
{
    if (image.width <= 0 || image.height <= 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height)
        return nullptr;
    auto texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
    if (texture && !SDL_UpdateTexture(texture, nullptr, image.pixels.data(), image.width * 4))
    {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    if (texture)
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return texture;
}
ImTextureID ImageID(SDL_Texture *texture)
{
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
}
struct View
{
    FileDialog &dialog;
    SDL_Texture *artwork = nullptr, *accept = nullptr, *cancel = nullptr, *home = nullptr, *preview = nullptr;
    bool initialized = false, firstFrame = true;
    std::string path, previewed;
    void Release()
    {
        for (auto texture : {artwork, accept, cancel, home, preview})
            SDL_DestroyTexture(texture);
        artwork = accept = cancel = home = preview = nullptr;
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
            path = browser.directory;
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
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::Begin("##portfolio", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_HorizontalScrollbar);
        const auto window = ImGui::GetWindowPos();
        const ImVec2 origin(window.x - ImGui::GetScrollX(), window.y - ImGui::GetScrollY());
        const auto pos = [&](float x, float y) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + x * scale, origin.y + y * scale));
        };
        const auto image = [&](SDL_Texture *texture, float x, float y, float w, float h) {
            if (texture)
                ImGui::GetWindowDrawList()->AddImage(ImageID(texture),
                                                     ImVec2(origin.x + x * scale, origin.y + y * scale),
                                                     ImVec2(origin.x + (x + w) * scale, origin.y + (y + h) * scale));
        };
        image(artwork, 0, 0, 640, 480);
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
            control.x = low.x - window.x;
            control.y = low.y - window.y;
            control.width = high.x - low.x;
            control.height = high.y - low.y;
            state.controls.push_back(std::move(control));
        };
        const auto button = [&](unsigned id, const char *label, float x, float y, float width, float height,
                                SDL_Texture *texture = nullptr) {
            if (texture)
                SDL_GetTextureSize(texture, &width, &height);
            pos(x, y);
            ImGui::PushID(static_cast<int>(id));
            bool clicked;
            if (texture)
            {
                clicked = ImGui::InvisibleButton(label, ImVec2(width * scale, height * scale));
                image(texture, x, y, width, height);
                if (ImGui::IsItemFocused())
                    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                        IM_COL32(255, 255, 255, 255));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", label);
            }
            else
                clicked = ImGui::Button(label, ImVec2(width * scale, height * scale));
            describe(id, label);
            ImGui::PopID();
            return clicked;
        };
        pos(10, 5);
        ImGui::TextUnformatted(dialog.title.c_str());
        pos(10, 26);
        ImGui::SetNextItemWidth(404 * scale);
        if (ImGui::InputText("##folder", &path, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (browser.Navigate(path))
                path = browser.directory;
        }
        describe(10, "Folder", KauaiDialogs::ItemKind::editText, path);
        if (button(4, "Up", 425, 26, 48, 24))
        {
            browser.Up();
            path = browser.directory;
        }
        if (button(3, "Home", 480, 26, 65, 24, home))
        {
            browser.Navigate(browser.home);
            path = browser.directory;
        }
        std::string navigate;
        bool choose = false;
        pos(10, 65);
        ImGui::BeginChild("##files", ImVec2(405 * scale, 155 * scale), ImGuiChildFlags_Borders);
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(browser.entries.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
            {
                const auto &entry = browser.entries[i];
                ImGui::PushID(i);
                const std::string label = (entry.directory ? "[+] " : "") + entry.name;
                if (ImGui::Selectable(label.c_str(), browser.name == entry.name, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    browser.Select(i);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        if (entry.directory)
                            navigate = entry.path;
                        else
                            choose = true;
                    }
                }
                describe(1000 + i, entry.name);
                ImGui::PopID();
            }
        ImGui::EndChild();
        if (!navigate.empty())
        {
            browser.Navigate(navigate);
            path = browser.directory;
        }
        image(preview, 56, 242, 144, 80);
        DialogControl thumbnail;
        thumbnail.id = 21;
        thumbnail.label = "Preview";
        thumbnail.number = preview ? 1 : 0;
        thumbnail.text = previewed;
        state.controls.push_back(thumbnail);
        pos(10, 350);
        ImGui::TextUnformatted("File name:");
        pos(10, 370);
        ImGui::SetNextItemWidth(405 * scale);
        if (firstFrame && !browser.open)
            ImGui::SetKeyboardFocusHere();
        choose |= ImGui::InputText("##filename", &browser.name, ImGuiInputTextFlags_EnterReturnsTrue);
        describe(11, "File name", KauaiDialogs::ItemKind::editText, browser.name);
        pos(10, 405);
        ImGui::SetNextItemWidth(405 * scale);
        const char *filterLabel = browser.filters.empty() ? "All files" : browser.filters[browser.filter].label.c_str();
        if (ImGui::BeginCombo("##filter", filterLabel))
        {
            for (unsigned i = 0; i < browser.filters.size(); ++i)
                if (ImGui::Selectable(browser.filters[i].label.c_str(), browser.filter == i))
                {
                    browser.filter = i;
                    browser.Refresh();
                }
            ImGui::EndCombo();
        }
        describe(12, "File type", KauaiDialogs::ItemKind::combo);
        if (button(2, "Cancel", 425, 400, 90, 55, cancel))
            session.Cancel();
        choose |= button(1, browser.open ? "Open" : "Save", 530, 400, 90, 55, accept);
        if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                session.Cancel();
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive())
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
                path = browser.directory;
        }
        pos(10, 450);
        ImGui::TextWrapped("%s", browser.error.c_str());
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(640 * scale, 480 * scale));
        ImGui::End();
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
                    [](void *context) { static_cast<View *>(context)->Release(); }};
    return RunModal(session, parent, idle, context, &modal);
}
} // namespace KauaiSDL
