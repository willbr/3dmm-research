/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_dialog.h"
#include "sdl_modal_internal.h"
#include "font_host.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "sdl_classic_dialog.h"
#include "sdl_dialog_font.h"
#include <algorithm>
#include <deque>
#include <list>
#include <mutex>
#include <cstring>
#include <cmath>

namespace KauaiSDL
{
namespace
{
std::mutex stateMutex;
DialogState published;
std::deque<DialogAction> actions;
std::uint64_t nextGeneration = 0;

struct RendererState
{
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *target = nullptr;
    int width = 0, height = 0;
    SDL_RendererLogicalPresentation presentation = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_Rect viewport{}, clip{};
    bool viewportSet = false, clipSet = false;
    float scaleX = 1, scaleY = 1;
    float red = 0, green = 0, blue = 0, alpha = 1;
    SDL_BlendMode blend = SDL_BLENDMODE_NONE;
    bool Capture(SDL_Renderer *value)
    {
        if (!SDL_GetRenderLogicalPresentation(value, &width, &height, &presentation))
            return false;
        renderer = value;
        target = SDL_GetRenderTarget(renderer);
        viewportSet = SDL_RenderViewportSet(renderer);
        clipSet = SDL_RenderClipEnabled(renderer);
        SDL_GetRenderViewport(renderer, &viewport);
        SDL_GetRenderClipRect(renderer, &clip);
        SDL_GetRenderScale(renderer, &scaleX, &scaleY);
        SDL_GetRenderDrawColorFloat(renderer, &red, &green, &blue, &alpha);
        SDL_GetRenderDrawBlendMode(renderer, &blend);
        return true;
    }
    void Restore()
    {
        if (!renderer)
            return;
        SDL_SetRenderTarget(renderer, target);
        SDL_SetRenderLogicalPresentation(renderer, width, height, presentation);
        SDL_SetRenderScale(renderer, scaleX, scaleY);
        SDL_SetRenderViewport(renderer, viewportSet ? &viewport : nullptr);
        SDL_SetRenderClipRect(renderer, clipSet ? &clip : nullptr);
        SDL_SetRenderDrawColorFloat(renderer, red, green, blue, alpha);
        SDL_SetRenderDrawBlendMode(renderer, blend);
    }
};

struct Window
{
    SDL_Window *window = nullptr, *parent = nullptr;
    SDL_Renderer *renderer = nullptr;
    ImGuiContext *previous = ImGui::GetCurrentContext(), *imgui = nullptr;
    SDL_Cursor *cursor = SDL_GetCursor();
    bool cursorVisible = SDL_CursorVisible(), initialized = false, platform = false, drawing = false, modal = false;
    bool clipboardChanged = false, quitRequested = false;
    bool embedded = false, ownsRenderer = true, textInputWasActive = false;
    bool mouseWasCaptured = false, textAreaKnown = false;
    SDL_Rect textArea{};
    int textCursor = 0;
    RendererState rendererState;
    std::list<std::string> textEvents;
    std::string fontFamily;
    ImVec2 units = ImVec2(2, 2);
    ImFont *captionFont = nullptr;
    float scale = 1;
    ~Window()
    {
        // Pushed text events borrow our strings. Drain them before releasing storage.
        if (!textEvents.empty())
            SDL_FlushEvent(SDL_EVENT_TEXT_INPUT);
        const bool focused = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
        if (imgui)
            ImGui::SetCurrentContext(imgui);
        if (drawing)
            ImGui_ImplSDLRenderer3_Shutdown();
        if (platform)
            ImGui_ImplSDL3_Shutdown();
        if (imgui)
            ImGui::DestroyContext(imgui);
        ImGui::SetCurrentContext(previous);
        rendererState.Restore();
        if (embedded && window)
        {
            SDL_CaptureMouse(mouseWasCaptured);
            if (textAreaKnown)
                SDL_SetTextInputArea(window, &textArea, textCursor);
            if (textInputWasActive)
                SDL_StartTextInput(window);
            else
                SDL_StopTextInput(window);
        }
        if (ownsRenderer)
            SDL_DestroyRenderer(renderer);
        if (modal)
            SDL_SetWindowModal(window, false);
        if (!embedded)
            SDL_DestroyWindow(window);
        if (cursor)
            SDL_SetCursor(cursor);
        cursorVisible ? SDL_ShowCursor() : SDL_HideCursor();
        if (!embedded && parent && focused)
            SDL_RaiseWindow(parent);
        if (previous)
        {
            auto &io = ImGui::GetIO();
            io.ClearInputKeys();
            io.ClearInputMouse();
            io.AddFocusEvent(parent && (SDL_GetWindowFlags(parent) & SDL_WINDOW_INPUT_FOCUS));
        }
        if (clipboardChanged)
        {
            // ImGui's plain-text clipboard is external to Kauai's rich data.
            SDL_Event changed{};
            changed.type = SDL_EVENT_CLIPBOARD_UPDATE;
            changed.clipboard.owner = false;
            SDL_PushEvent(&changed);
        }
        if (quitRequested)
        {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    void Key(SDL_Scancode scan, bool down, SDL_Keymod modifiers = SDL_KMOD_NONE)
    {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(window);
        event.key.scancode = scan;
        event.key.key = SDL_GetKeyFromScancode(scan, modifiers, false);
        event.key.mod = modifiers;
        event.key.down = down;
        SDL_PushEvent(&event);
    }
    void Click(const DialogControl &control)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = SDL_GetWindowID(window);
        event.motion.x = control.x + control.width / 2;
        event.motion.y = control.y + control.height / 2;
        SDL_PushEvent(&event);
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.windowID = SDL_GetWindowID(window);
        event.button.x = control.x + control.width / 2;
        event.button.y = control.y + control.height / 2;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = true;
        SDL_PushEvent(&event);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.down = false;
        SDL_PushEvent(&event);
    }
};
SDL_HitTestResult SDLCALL CaptionHit(SDL_Window *window, const SDL_Point *point, void *context)
{
    const float scale = static_cast<Window *>(context)->scale;
    int width = 0;
    SDL_GetWindowSize(window, &width, nullptr);
    return point->y >= ClassicDialog::Border * scale &&
                   point->y < (ClassicDialog::Border + ClassicDialog::CaptionHeight) * scale &&
                   point->x >= ClassicDialog::Border * scale && point->x < width - 24 * scale
               ? SDL_HITTEST_DRAGGABLE
               : SDL_HITTEST_NORMAL;
}
void SelectDialogFont(Window &scope)
{
    for (const char *family :
         {"MS Sans Serif", "Microsoft Sans Serif", "Tahoma", "Arial", "Liberation Sans", "DejaVu Sans", "Helvetica"})
    {
        KauaiFonts::Text name{family, std::strlen(family)};
        auto face = KauaiFonts::CreateStudioFace(name, DialogFont::Pixels, false, false, false);
        KauaiFonts::Metrics metrics;
        const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        if (!face || !face->Measure({alphabet, sizeof(alphabet) - 1}, metrics))
            continue;
        scope.fontFamily = family;
        // Windows dialog base units: rounded average alphabet width / 4,
        // and the font's cell height / 8. Keep the original resource layout.
        scope.units.x = std::max(1.f, float((metrics.advance / 26 + 1) / 2) / 4);
        scope.units.y = std::max(1.f, float(metrics.cellHeight) / 8);
        return;
    }
}
void DialogFonts(Window &scope)
{
    if (scope.fontFamily.empty())
        return;
    ImGui::GetIO().FontDefault = DialogFont::Add(scope.fontFamily.c_str());
    scope.captionFont = DialogFont::Add(scope.fontFamily.c_str(), true);
}
struct Publication
{
    DialogState previous;
    std::deque<DialogAction> previousActions;
    std::uint64_t generation;
    Publication()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        previous = std::move(published);
        published = {};
        previousActions.swap(actions);
        generation = ++nextGeneration;
    }
    ~Publication()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        published = std::move(previous);
        published.ready = false;
        actions.swap(previousActions);
    }
};
DialogControl Describe(const KauaiDialogs::Control &control, const KauaiDialogs::Session &session)
{
    DialogControl result;
    result.id = control.id;
    result.item = session.Item(control.id);
    result.label = KauaiDialogs::Caption(control.title.text, true);
    result.enabled = !(control.style & 0x08000000); // WS_DISABLED.
    result.readOnly = (control.windowClass.number == 0x81 && (control.style & 0x800)) ||
                      (control.windowClass.number == 0x85 && (control.style & 3) != 2);
    if (result.item >= 0)
    {
        result.kind = session.bindings[result.item].kind;
        result.number = session.values[result.item].number;
        result.text = session.values[result.item].text;
    }
    return result;
}
DialogState Draw(KauaiDialogs::Session &session, float scale, std::uint32_t window, std::uint64_t generation,
                 bool active, ImFont *captionFont, ImVec2 units)
{
    using namespace KauaiDialogs;
    DialogState state;
    state.active = true;
    state.ready = true;
    state.generation = generation;
    state.window = window;
    state.title = Caption(session.resource.title);
    const auto viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("##original-dialog", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    const auto windowPosition = viewport->Pos;
    auto frame = ImGui::GetWindowDrawList();
    const ImVec2 outer(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    ClassicDialog::Bevel(frame, viewport->Pos, outer, false, scale);
    const ImVec2 titleLow(viewport->Pos.x + ClassicDialog::Border * scale,
                          viewport->Pos.y + ClassicDialog::Border * scale);
    const ImVec2 titleHigh(outer.x - ClassicDialog::Border * scale, titleLow.y + ClassicDialog::CaptionHeight * scale);
    frame->AddRectFilled(titleLow, titleHigh, active ? ClassicDialog::Navy : ClassicDialog::Shadow);
    ImGui::PushFont(captionFont);
    frame->PushClipRect(titleLow, ImVec2(titleHigh.x - 20 * scale, titleHigh.y), true);
    frame->AddText(ImVec2(titleLow.x + 3 * scale, titleLow.y + 2 * scale), ClassicDialog::White, state.title.c_str());
    frame->PopClipRect();
    ImGui::PopFont();
    const ImVec2 closeLow(titleHigh.x - 18 * scale, titleLow.y + 2 * scale);
    ImGui::SetCursorScreenPos(closeLow);
    ImGui::BeginDisabled(!session.cancelControl);
    if (ImGui::InvisibleButton("##caption-close", ImVec2(16 * scale, 14 * scale)))
        session.Cancel();
    ClassicDialog::CloseButton(frame, closeLow, ImGui::IsItemActive(), session.cancelControl != 0, scale);
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(ImVec2(titleLow.x, viewport->Pos.y + ClassicDialog::ContentTop * scale));
    ImGui::BeginChild(
        "##dialog-content",
        ImVec2(std::max(1.f, viewport->Size.x - 2 * ClassicDialog::Border * scale),
               std::max(1.f, viewport->Size.y - (ClassicDialog::ContentTop + ClassicDialog::Border) * scale)),
        0, ImGuiWindowFlags_HorizontalScrollbar);
    const auto contentPosition = ImGui::GetWindowPos();
    const ImVec2 origin(contentPosition.x - ImGui::GetScrollX(), contentPosition.y - ImGui::GetScrollY());
    const auto savedFocus = session.focus;
    const auto &tabs = session.resource.tabs;
    if (!tabs.pages.empty())
    {
        const auto &bounds = tabs.bounds;
        const float header = ImGui::GetTextLineHeight() + 8 * scale;
        const ImVec2 low(origin.x + bounds.x * units.x * scale, origin.y + bounds.y * units.y * scale);
        const ImVec2 high(low.x + bounds.width * units.x * scale, low.y + bounds.height * units.y * scale);
        auto draw = ImGui::GetWindowDrawList();
        ClassicDialog::Bevel(draw, ImVec2(low.x, low.y + header), high, false, scale);
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
            session.selectedTab = ImGui::GetIO().KeyShift
                                      ? (session.selectedTab + tabs.pages.size() - 1) % tabs.pages.size()
                                      : (session.selectedTab + 1) % tabs.pages.size();
        struct TabVisual
        {
            ImVec2 low, high;
            std::string label;
            bool focused;
        };
        std::vector<TabVisual> visuals;
        float total = 0;
        for (const auto &page : tabs.pages)
        {
            auto label = KauaiDialogs::Caption(page.title);
            const float width = ImGui::CalcTextSize(label.c_str()).x + 14 * scale;
            visuals.push_back({{}, {width, 0}, std::move(label), false});
            total += width;
        }
        const float fit = std::min(1.f, (high.x - low.x - 4 * scale) / total);
        float left = low.x + 2 * scale;
        for (std::size_t index = 0; index < tabs.pages.size(); ++index)
        {
            const auto &page = tabs.pages[index];
            auto &visual = visuals[index];
            const float width = visual.high.x * fit;
            visual.low = ImVec2(left, low.y + 2 * scale);
            visual.high = ImVec2(left + width, low.y + header);
            left += width;
            ImGui::SetCursorScreenPos(visual.low);
            ImGui::PushID(static_cast<int>(page.id));
            if (ImGui::InvisibleButton("##tab", ImVec2(width, header - 2 * scale)))
                session.selectedTab = index;
            visual.focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
            if (ImGui::IsItemFocused())
            {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
                    session.selectedTab = (session.selectedTab + tabs.pages.size() - 1) % tabs.pages.size();
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
                    session.selectedTab = (session.selectedTab + 1) % tabs.pages.size();
            }
            DialogControl control;
            control.id = page.id;
            control.item = static_cast<std::int32_t>(page.id);
            control.label = visual.label;
            control.kind = ItemKind::radioGroup;
            control.x = visual.low.x - viewport->Pos.x;
            control.y = visual.low.y - viewport->Pos.y;
            control.width = width;
            control.height = header - 2 * scale;
            state.controls.push_back(std::move(control));
            ImGui::PopID();
        }
        const auto paint = [&](std::size_t index) {
            const auto &visual = visuals[index];
            const bool selected = session.selectedTab == index;
            const ImVec2 tabLow(visual.low.x - (selected ? 2 * scale : 0), visual.low.y - (selected ? 2 * scale : 0));
            const ImVec2 tabHigh(visual.high.x + (selected ? 2 * scale : 0),
                                 visual.high.y + (selected ? 2 * scale : 0));
            ClassicDialog::Tab(draw, tabLow, tabHigh, selected, scale);
            const auto text = ImGui::CalcTextSize(visual.label.c_str());
            const ImVec2 label(visual.low.x + (visual.high.x - visual.low.x - text.x) / 2,
                               visual.low.y + (header - 2 * scale - text.y) / 2);
            draw->PushClipRect(ImVec2(tabLow.x + 3 * scale, tabLow.y), ImVec2(tabHigh.x - 3 * scale, tabHigh.y), true);
            draw->AddText(label, ClassicDialog::Black, visual.label.c_str());
            if (visual.focused)
                ClassicDialog::Focus(draw, ImVec2(label.x - 3 * scale, label.y - scale),
                                     ImVec2(label.x + text.x + 3 * scale, label.y + text.y + scale), scale);
            draw->PopClipRect();
        };
        for (std::size_t index = 0; index < visuals.size(); ++index)
            if (index != session.selectedTab)
                paint(index);
        paint(session.selectedTab);
        for (std::size_t index = 0; index < tabs.pages.size(); ++index)
            state.controls[index].number = index == session.selectedTab;
    }

    std::int32_t defaultButton = -1;
    bool multilineActive = false;
    for (std::size_t index = 0; index < session.resource.controls.size() && !session.closed; ++index)
    {
        const auto &control = session.resource.controls[index];
        if (!tabs.Visible(control.id, session.selectedTab))
            continue;
        if (!(control.style & 0x10000000))
            continue;
        const auto descriptor = Describe(control, session);
        auto geometry = descriptor;
        const auto &bounds = control.bounds;
        const ImVec2 position(origin.x + bounds.x * units.x * scale, origin.y + bounds.y * units.y * scale);
        const ImVec2 dimensions(std::max(1.f, bounds.width * units.x * scale),
                                std::max(1.f, bounds.height * units.y * scale));
        ImGui::SetCursorScreenPos(position);
        ImGui::PushID(static_cast<int>(index));
        ImGui::BeginDisabled(!descriptor.enabled);
        bool changed = false, interactive = false;
        const auto item = descriptor.item;
        const auto buttonStyle = control.style & 15;
        if (control.windowClass.number == 0x80 && buttonStyle == 7)
        {
            auto draw = ImGui::GetWindowDrawList();
            const float textHeight = ImGui::GetFontSize();
            ClassicDialog::Edge(draw, ImVec2(position.x + scale, position.y + textHeight / 2 + scale),
                                ImVec2(position.x + dimensions.x + scale, position.y + dimensions.y + scale),
                                ClassicDialog::White, ClassicDialog::White, scale);
            ClassicDialog::Edge(draw, ImVec2(position.x, position.y + textHeight / 2),
                                ImVec2(position.x + dimensions.x, position.y + dimensions.y), ClassicDialog::Shadow,
                                ClassicDialog::Shadow, scale);
            const auto size = ImGui::CalcTextSize(descriptor.label.c_str());
            draw->AddRectFilled(ImVec2(position.x + 5 * scale, position.y),
                                ImVec2(position.x + 12 * scale + size.x, position.y + size.y),
                                ImGui::GetColorU32(ImGuiCol_WindowBg));
            draw->AddText(ImVec2(position.x + 8 * scale, position.y), ImGui::GetColorU32(ImGuiCol_Text),
                          descriptor.label.c_str());
        }
        else if (control.windowClass.number == 0x82)
        {
            float offset = 0;
            const auto alignment = control.style & 31;
            const auto textWidth = ImGui::CalcTextSize(descriptor.label.c_str(), nullptr, false, dimensions.x).x;
            if (alignment == 1)
                offset = std::max(0.f, (dimensions.x - textWidth) / 2);
            if (alignment == 2)
                offset = std::max(0.f, dimensions.x - textWidth);
            ImGui::SetCursorScreenPos(ImVec2(position.x + offset, position.y));
            ImGui::PushTextWrapPos((bounds.x + bounds.width) * units.x * scale);
            ImGui::TextUnformatted(descriptor.label.c_str());
            ImGui::PopTextWrapPos();
        }
        else if (item >= 0)
        {
            auto &value = session.values[item];
            const auto kind = session.bindings[item].kind;
            auto draw = ImGui::GetWindowDrawList();
            const bool decorated =
                kind == ItemKind::button || kind == ItemKind::checkBox || kind == ItemKind::radioGroup;
            if (!descriptor.enabled)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (decorated)
            {
                draw->ChannelsSplit(2);
                draw->ChannelsSetCurrent(1);
                for (auto color : {ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive, ImGuiCol_FrameBg,
                                   ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_CheckMark})
                    ImGui::PushStyleColor(color, ImVec4(0, 0, 0, 0));
            }
            if (savedFocus == item)
                ImGui::SetKeyboardFocusHere();
            if (kind == ItemKind::button)
            {
                if (buttonStyle == 1 && descriptor.enabled)
                    defaultButton = item;
                changed = ImGui::Button(descriptor.label.c_str(), dimensions);
            }
            else if (kind == ItemKind::checkBox)
            {
                bool checked = value.number != 0;
                changed = ImGui::Checkbox(descriptor.label.c_str(), &checked);
                if (changed && buttonStyle == 3)
                    value.number = checked;
            }
            else if (kind == ItemKind::radioGroup)
            {
                const auto choice = static_cast<std::int32_t>(control.id - session.bindings[item].first);
                changed = ImGui::RadioButton(descriptor.label.c_str(), value.number == choice);
                if (changed && buttonStyle == 9)
                    value.number = choice;
            }
            else if (kind == ItemKind::editText)
            {
                ImGui::PushStyleColor(
                    ImGuiCol_FrameBg,
                    ImGui::ColorConvertU32ToFloat4(descriptor.readOnly ? ClassicDialog::Face : ClassicDialog::White));
                ImGuiInputTextFlags flags =
                    descriptor.readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
                if (control.style & 0x20)
                    flags |= ImGuiInputTextFlags_Password;
                if (savedFocus == item)
                    flags |= ImGuiInputTextFlags_AutoSelectAll;
                ImGui::SetNextItemWidth(dimensions.x);
                if (control.style & 4)
                {
                    if (!(control.style & 0x1000))
                        flags |= ImGuiInputTextFlags_CtrlEnterForNewLine;
                    changed = ImGui::InputTextMultiline("##value", &value.text, dimensions, flags);
                    multilineActive |= ImGui::IsItemActive();
                }
                else
                    changed = ImGui::InputText("##value", &value.text, flags);
                ImGui::PopStyleColor();
            }
            else if (kind == ItemKind::combo && (control.style & 3) == 1)
            {
                // A simple combo supplies the legacy list model without a
                // popup. Use its full resource bounds for a visible list pane.
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 1));
                ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(ClassicDialog::Navy));
                if (ImGui::BeginListBox("##list", dimensions))
                {
                    ImGuiListClipper clip;
                    clip.Begin(int(value.choices.size()));
                    while (clip.Step())
                        for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row)
                        {
                            const auto &choice = value.choices[row];
                            ImGui::PushID(row);
                            const bool selected = value.text == choice;
                            if (selected)
                                ImGui::PushStyleColor(ImGuiCol_Text,
                                                      ImGui::ColorConvertU32ToFloat4(ClassicDialog::White));
                            if (ImGui::Selectable(choice.c_str(), selected))
                            {
                                value.text = choice;
                                changed = true;
                            }
                            if (selected)
                                ImGui::PopStyleColor();
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", choice.c_str());
                            if (ImGui::IsItemVisible())
                            {
                                DialogControl entry;
                                entry.id = 0x10000000u + (control.id << 16) + unsigned(row);
                                entry.item = item;
                                entry.label = entry.text = choice;
                                entry.number = selected;
                                entry.enabled = descriptor.enabled;
                                const auto low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
                                entry.x = low.x - windowPosition.x;
                                entry.y = low.y - windowPosition.y;
                                entry.width = high.x - low.x;
                                entry.height = high.y - low.y;
                                state.controls.push_back(std::move(entry));
                            }
                            ImGui::PopID();
                        }
                    ImGui::EndListBox();
                }
                ImGui::PopStyleColor(2);
            }
            else if (kind == ItemKind::combo)
            {
                const bool editable = (control.style & 3) != 3;
                if (editable)
                {
                    ImGui::SetNextItemWidth(std::max(1.f, dimensions.x - ImGui::GetFrameHeight()));
                    ImGui::InputText("##editable-choice", &value.text);
                    ImGui::SameLine(0, 0);
                }
                else
                    ImGui::SetNextItemWidth(dimensions.x);
                if (ImGui::BeginCombo("##choices", editable ? nullptr : value.text.c_str(),
                                      editable ? ImGuiComboFlags_NoPreview : ImGuiComboFlags_None))
                {
                    for (const auto &choice : value.choices)
                        if (ImGui::Selectable(choice.c_str(), choice == value.text))
                            value.text = choice;
                    ImGui::EndCombo();
                }
            }
            interactive = true;
            const auto low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
            if (decorated)
            {
                draw->ChannelsSetCurrent(0);
                if (kind == ItemKind::button)
                    ClassicDialog::Button(draw, low, high, ImGui::IsItemActive(), ImGui::IsItemFocused(),
                                          buttonStyle == 1, scale);
                else
                {
                    const bool radio = kind == ItemKind::radioGroup;
                    const bool checked =
                        radio ? value.number == static_cast<std::int32_t>(control.id - session.bindings[item].first)
                              : value.number != 0;
                    ClassicDialog::Check(draw, low, checked, descriptor.enabled, radio, scale);
                    if (ImGui::IsItemFocused())
                        ClassicDialog::Focus(draw, ImVec2(low.x + ImGui::GetFrameHeight() + 2 * scale, low.y), high,
                                             scale);
                }
                draw->ChannelsMerge();
                ImGui::PopStyleColor(7);
            }
            else if (kind == ItemKind::editText && (control.style & 0x00800000 || control.extendedStyle & 0x200))
                ClassicDialog::Bevel(draw, low, high, true, scale);
            if (!descriptor.enabled)
                ImGui::PopStyleColor();
            geometry.x = low.x - windowPosition.x;
            geometry.y = low.y - windowPosition.y;
            geometry.width = high.x - low.x;
            geometry.height = high.y - low.y;
            if (kind == ItemKind::combo && (control.style & 3) != 1)
            {
                geometry.x = position.x - windowPosition.x;
                geometry.y = position.y - windowPosition.y;
                geometry.width = dimensions.x;
                geometry.height = ImGui::GetFrameHeight();
            }
            if (changed)
                session.Notify(item);
        }
        if (!interactive)
        {
            geometry.x = position.x - windowPosition.x;
            geometry.y = position.y - windowPosition.y;
            geometry.width = dimensions.x;
            geometry.height = dimensions.y;
        }
        state.controls.push_back(std::move(geometry));
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    // Keep the complete resource canvas scrollable, including draw-list-only
    // group boxes and empty trailing space. Scrolling never rescales controls.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(
        ImVec2(session.resource.bounds.width * units.x * scale, session.resource.bounds.height * units.y * scale));
    if (session.focus == savedFocus)
        session.focus = -1;
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            session.Cancel();
        if (!multilineActive && defaultButton >= 0 && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            session.Notify(defaultButton);
    }
    ImGui::EndChild();
    ImGui::End();
    return state;
}
} // namespace
DialogState CurrentDialog()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return published;
}
bool QueueDialogAction(std::uint64_t generation, const DialogAction &action)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!published.active || !published.ready || published.generation != generation)
        return false;
    if (action.kind == DialogActionKind::click || action.kind == DialogActionKind::text)
    {
        const auto found = std::find_if(published.controls.begin(), published.controls.end(),
                                        [&](const DialogControl &control) { return control.id == action.control; });
        if (found == published.controls.end() || found->item < 0 || !found->enabled ||
            (action.kind == DialogActionKind::text &&
             (found->readOnly ||
              (found->kind != KauaiDialogs::ItemKind::editText && found->kind != KauaiDialogs::ItemKind::combo))))
            return false;
    }
    actions.push_back(action);
    return true;
}
bool RunDialog(KauaiDialogs::Session &session, std::uint32_t parentID, void (*idle)(void *), void *context)
{
    return RunModal(session, parentID, idle, context, nullptr);
}
bool RunModal(KauaiDialogs::Session &session, std::uint32_t parentID, void (*idle)(void *), void *context,
              ModalView *view)
{
    bool shown = false;
    try
    {
        const auto outer = CurrentDialog();
        if (outer.active)
            parentID = outer.window;
        Window scope;
        struct ReleaseView
        {
            ModalView *view;
            ~ReleaseView()
            {
                if (view && view->release)
                    view->release(view->context);
            }
        } release{view};
        SDL_SetMainReady();
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            return false;
        scope.initialized = true;
        scope.parent = parentID ? SDL_GetWindowFromID(parentID) : nullptr;
        if (parentID && !scope.parent)
            return false;
        float scale = std::max(1.f, SDL_GetDisplayContentScale(scope.parent ? SDL_GetDisplayForWindow(scope.parent)
                                                                            : SDL_GetPrimaryDisplay()));
        scope.scale = scale;
        SelectDialogFont(scope);
        if (view)
            scope.units = ImVec2(2, 2); // Portfolio artwork retains its 640x480 canvas.
        const auto title = KauaiDialogs::Caption(session.resource.title);
        scope.embedded = view && view->inParent && scope.parent;
        if (scope.embedded)
            scope.window = scope.parent;
        else
        {
            scope.window = SDL_CreateWindow(
                view && scope.parent ? SDL_GetWindowTitle(scope.parent) : title.c_str(),
                static_cast<int>(
                    (session.resource.bounds.width * scope.units.x + (view ? 0 : 2 * ClassicDialog::Border)) * scale),
                static_cast<int>((session.resource.bounds.height * scope.units.y +
                                  (view ? 0 : ClassicDialog::ContentTop + ClassicDialog::Border)) *
                                 scale),
                SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN | (view ? 0 : SDL_WINDOW_BORDERLESS));
            if (!scope.window)
                return false;
            if (!view)
                SDL_SetWindowHitTest(scope.window, CaptionHit, &scope);
            if (scope.parent)
            {
                // The dummy driver has no window manager. Run the same modal
                // session/event loop without requesting native window modality.
                const auto driver = SDL_GetCurrentVideoDriver();
                if (!driver || SDL_strcmp(driver, "dummy") != 0)
                {
                    if (!SDL_SetWindowParent(scope.window, scope.parent) || !SDL_SetWindowModal(scope.window, true))
                        return false;
                    scope.modal = true;
                }
                int x = 0, y = 0, w = 0, h = 0, dw = 0, dh = 0;
                if (SDL_GetWindowPosition(scope.parent, &x, &y) && SDL_GetWindowSize(scope.parent, &w, &h) &&
                    SDL_GetWindowSize(scope.window, &dw, &dh))
                    SDL_SetWindowPosition(scope.window, x + (w - dw) / 2, y + (h - dh) / 2);
            }
            SDL_Rect usable;
            if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(scope.window), &usable))
            {
                int top = 0, left = 0, bottom = 0, right = 0;
                SDL_GetWindowBordersSize(scope.window, &top, &left, &bottom, &right);
                int x, y, width, height;
                SDL_GetWindowPosition(scope.window, &x, &y);
                SDL_GetWindowSize(scope.window, &width, &height);
                width = std::min(width, std::max(1, usable.w - left - right));
                height = std::min(height, std::max(1, usable.h - top - bottom));
                SDL_SetWindowSize(scope.window, width, height);
                x = std::max(usable.x + left, std::min(x, usable.x + usable.w - right - width));
                y = std::max(usable.y + top, std::min(y, usable.y + usable.h - bottom - height));
                SDL_SetWindowPosition(scope.window, x, y);
            }
        }
        scope.textInputWasActive = SDL_TextInputActive(scope.window);
        if (scope.embedded)
        {
            scope.mouseWasCaptured = (SDL_GetWindowFlags(scope.window) & SDL_WINDOW_MOUSE_CAPTURE) != 0;
            scope.textAreaKnown = SDL_GetTextInputArea(scope.window, &scope.textArea, &scope.textCursor);
        }
        scope.renderer = scope.embedded ? SDL_GetRenderer(scope.window) : nullptr;
        scope.ownsRenderer = !scope.renderer;
        if (scope.renderer && !scope.rendererState.Capture(scope.renderer))
            return false;
        if (!scope.renderer)
            scope.renderer = SDL_CreateRenderer(scope.window, nullptr);
        if (!scope.renderer)
            return false;
        if (scope.embedded)
        {
            // ImGui uses window coordinates. Render the fitted portfolio in
            // those coordinates, restoring Studio's logical presentation later.
            if (!SDL_SetRenderTarget(scope.renderer, nullptr) ||
                !SDL_SetRenderLogicalPresentation(scope.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED))
                return false;
            SDL_SetRenderViewport(scope.renderer, nullptr);
            SDL_SetRenderClipRect(scope.renderer, nullptr);
            SDL_SetRenderScale(scope.renderer, 1, 1);
        }
        scope.imgui = ImGui::CreateContext();
        ImGui::SetCurrentContext(scope.imgui);
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ClassicDialog::Style(scale);
        DialogFonts(scope);
        if (view && view->initialize)
            view->initialize(view->context);
        scope.platform = ImGui_ImplSDL3_InitForSDLRenderer(scope.window, scope.renderer);
        if (!scope.platform)
            return false;
        scope.drawing = ImGui_ImplSDLRenderer3_Init(scope.renderer);
        if (!scope.drawing || (!scope.embedded && !SDL_ShowWindow(scope.window)))
            return false;
        SDL_StartTextInput(scope.window);
        shown = true;
        Publication publication;
        const auto id = SDL_GetWindowID(scope.window);
        std::uint64_t tick = 0;
        unsigned focusSettling = 2;
        while (!session.closed)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == parentID))
                {
                    scope.quitRequested = session.quitRequested = true;
                    session.closed = true;
                    session.result = -1;
                }
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == id)
                    session.Cancel();
                if (event.type == SDL_EVENT_CLIPBOARD_UPDATE)
                    scope.clipboardChanged = true;
                ImGui_ImplSDL3_ProcessEvent(&event);
            }
            if (session.closed)
                break;
            if (idle)
                idle(context);
            if (SDL_GetTicks() - tick >= 10)
            {
                session.Notify(-1);
                tick = SDL_GetTicks();
            }
            if (session.closed)
                break;
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            if (scope.embedded)
            {
                int width = 0, height = 0;
                if (SDL_GetRenderOutputSize(scope.renderer, &width, &height) && width > 0 && height > 0)
                {
                    float pixels = std::min(width / 640.f, height / 480.f);
                    if (scope.rendererState.presentation == SDL_LOGICAL_PRESENTATION_INTEGER_SCALE && pixels >= 1)
                        pixels = std::floor(pixels);
                    scale = std::max(.05f, pixels / std::max(.01f, io.DisplayFramebufferScale.x));
                }
                else
                    scale = scope.scale; // A minimized window may temporarily have no drawable area.
                if (scale != scope.scale)
                {
                    // ScaleAllSizes multiplies the existing style. Start from
                    // its base sizes whenever the window's fitted scale changes.
                    ImGui::GetStyle() = ImGuiStyle();
                    ClassicDialog::Style(scale);
                    scope.scale = scale;
                }
            }
            ImGui::NewFrame();
            const bool focusing = session.focus >= 0;
            DialogState snapshot;
            if (view)
            {
                snapshot.active = snapshot.ready = true;
                snapshot.embedded = scope.embedded;
                snapshot.window = id;
                snapshot.generation = publication.generation;
                snapshot.title = title;
                view->draw(view->context, session, scale, snapshot, scope.renderer);
            }
            else
                snapshot = Draw(session, scale, id, publication.generation,
                                (SDL_GetWindowFlags(scope.window) & SDL_WINDOW_INPUT_FOCUS) != 0, scope.captionFont,
                                scope.units);
            // ImGui applies requested keyboard focus across frames. Publish a
            // ready layout after it settles so queued clicks cannot race a
            // callback's SelectDit request and lose their active widget.
            if (focusing || session.focus >= 0)
                focusSettling = 2;
            else if (focusSettling)
                --focusSettling;
            snapshot.ready = focusSettling == 0;
            ImGui::Render();
            SDL_SetRenderScale(scope.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            const Uint8 background = scope.embedded ? 0 : 28;
            SDL_SetRenderDrawColor(scope.renderer, background, background, background, 255);
            if (!SDL_RenderClear(scope.renderer))
            {
                session.closed = true;
                session.result = -1;
                break;
            }
            if (view && view->background)
                view->background(view->context, scope.renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), scope.renderer);
            std::deque<DialogAction> pending;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                published = std::move(snapshot);
                pending.swap(actions);
            }
            for (const auto &action : pending)
            {
                if (action.kind == DialogActionKind::screenshot)
                {
                    auto pixels = SDL_RenderReadPixels(scope.renderer, nullptr);
                    if (pixels)
                        SDL_SaveBMP(pixels, action.text.c_str());
                    SDL_DestroySurface(pixels);
                    continue;
                }
                if (action.kind == DialogActionKind::enter || action.kind == DialogActionKind::escape ||
                    action.kind == DialogActionKind::tab)
                {
                    const auto key = action.kind == DialogActionKind::enter    ? SDL_SCANCODE_RETURN
                                     : action.kind == DialogActionKind::escape ? SDL_SCANCODE_ESCAPE
                                                                               : SDL_SCANCODE_TAB;
                    scope.Key(key, true);
                    scope.Key(key, false);
                    continue;
                }
                const auto state = CurrentDialog();
                const auto found =
                    std::find_if(state.controls.begin(), state.controls.end(),
                                 [&](const DialogControl &control) { return control.id == action.control; });
                if (found == state.controls.end())
                    continue;
                scope.Click(*found);
                if (action.kind == DialogActionKind::text)
                {
                    const auto shortcut = io.ConfigMacOSXBehaviors ? SDL_SCANCODE_LGUI : SDL_SCANCODE_LCTRL;
                    const auto modifiers = io.ConfigMacOSXBehaviors ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
                    scope.Key(shortcut, true, modifiers);
                    scope.Key(SDL_SCANCODE_A, true, modifiers);
                    scope.Key(SDL_SCANCODE_A, false, modifiers);
                    scope.Key(shortcut, false);
                    scope.textEvents.push_back(action.text);
                    SDL_Event text{};
                    text.type = SDL_EVENT_TEXT_INPUT;
                    text.text.windowID = id;
                    text.text.text = scope.textEvents.back().c_str();
                    SDL_PushEvent(&text);
                }
            }
            SDL_RenderPresent(scope.renderer);
            SDL_Delay(8);
        }
        return true;
    }
    catch (const std::exception &)
    {
        session.closed = true;
        session.result = -1;
        return shown;
    }
}
} // namespace KauaiSDL
