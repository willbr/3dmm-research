#include "prop_gizmo.h"
#include "camera_guide.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "ImGuizmo.h"
#include <algorithm>
#include <cmath>
#include <memory>

struct MarcusPropGizmo
{
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    ImGuiContext *context = nullptr;
    MarcusPlayer *player = nullptr;
    int object = -1, scene = -1, editing = 0, changed = 0;
    long frame = 0;
    unsigned long document = 0, chunk = 0;
    bool enabled = false, cancel = false, finish = false, platform = false, render = false;
    bool over = false, fields = false;
    SDL_FRect fieldRect = {};
    float original[3] = {}, value[3] = {}, applied[3] = {};
    char error[1024] = {};

    void StoreError() { SDL_strlcpy(error, marcus_error(player), sizeof(error)); }

    bool SameContext() const
    {
        MarcusInfo info;
        MarcusObject prop;
        return player && marcus_document(player) == document && marcus_info(player, &info) &&
            info.scene == scene && info.frame == frame && marcus_object(player, object, &prop) && prop.chunk == chunk;
    }
    void Start(int kind)
    {
        if (editing) return;
        std::copy(value, value + 3, original);
        std::copy(value, value + 3, applied);
        editing = kind;
    }
    int Restore()
    {
        if (!editing || !SameContext()) return 0;
        return marcus_preview_prop_position(player, object, original[0], original[1], original[2]) ? 1 : -1;
    }
};

MarcusPropGizmo *prop_gizmo_create(SDL_Window *window, SDL_Renderer *renderer)
{
    auto gizmo = std::make_unique<MarcusPropGizmo>();
    gizmo->window = window;
    gizmo->renderer = renderer;
    IMGUI_CHECKVERSION();
    gizmo->context = ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.WindowBorderSize = 0;
    // Keep clipped position fields compact while satisfying ImGui's >=1 minimum.
    style.WindowMinSize = ImVec2(1, 1);
    style.FrameRounding = 2;
    style.Colors[ImGuiCol_FrameBg] = ImVec4(.15f, .18f, .21f, 1);
    gizmo->platform = ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    if (gizmo->platform) gizmo->render = ImGui_ImplSDLRenderer3_Init(renderer);
    if (!gizmo->render) { prop_gizmo_destroy(gizmo.release()); return nullptr; }
    return gizmo.release();
}
void prop_gizmo_destroy(MarcusPropGizmo *gizmo)
{
    if (!gizmo) return;
    ImGui::SetCurrentContext(gizmo->context);
    // The owning player can already have been replaced during shutdown.
    if (gizmo->render) ImGui_ImplSDLRenderer3_Shutdown();
    if (gizmo->platform) ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(gizmo->context);
    delete gizmo;
}
int prop_gizmo_event(MarcusPropGizmo *gizmo, const SDL_Event *event)
{
    if (!gizmo) return 0;
    ImGui::SetCurrentContext(gizmo->context);
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST && gizmo->editing) gizmo->cancel = true;
    if (!gizmo->enabled) return 0;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE && gizmo->editing)
    { gizmo->cancel = true; return 1; }
    const auto &io = ImGui::GetIO();
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_Z &&
        (event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) &&
        !(event->key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) && gizmo->editing && !io.WantTextInput)
    { gizmo->cancel = true; return 1; }
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP || event->type == SDL_EVENT_TEXT_INPUT)
        return gizmo->editing || io.WantCaptureKeyboard;
    if (event->type == SDL_EVENT_MOUSE_WHEEL) return gizmo->editing != 0;
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event->type == SDL_EVENT_MOUSE_MOTION)
    {
        float x = io.MousePos.x, y = io.MousePos.y;
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
        { x = event->button.x; y = event->button.y; }
        if (event->type == SDL_EVENT_MOUSE_MOTION) { x = event->motion.x; y = event->motion.y; }
        const auto &r = gizmo->fieldRect;
        const bool inFields = gizmo->fields && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
        const bool left = event->type == SDL_EVENT_MOUSE_MOTION || event->button.button == SDL_BUTTON_LEFT;
        return gizmo->editing || (left && (gizmo->over || inFields));
    }
    return 0;
}
void prop_gizmo_begin(MarcusPropGizmo *gizmo, MarcusPlayer *player, int object, int enabled, int width, int height)
{
    ImGui::SetCurrentContext(gizmo->context);
    gizmo->changed = 0;
    gizmo->error[0] = 0;
    MarcusObject prop;
    MarcusInfo info;
    const bool eligible = enabled && player && marcus_object(player, object, &prop) &&
        prop.prop && !prop.text && prop.visible && marcus_info(player, &info);
    if (gizmo->editing && (!eligible || player != gizmo->player || object != gizmo->object || !gizmo->SameContext()))
    {
        gizmo->changed = player == gizmo->player ? gizmo->Restore() : 0;
        if (gizmo->changed < 0) gizmo->StoreError();
        gizmo->editing = 0;
        gizmo->cancel = false;
        ImGuizmo::Enable(false);
    }
    gizmo->enabled = eligible;
    gizmo->player = player;
    gizmo->object = object;
    gizmo->over = gizmo->fields = false;
    gizmo->finish = false;
    if (eligible)
    {
        gizmo->document = marcus_document(player);
        gizmo->scene = info.scene;
        gizmo->frame = info.frame;
        gizmo->chunk = prop.chunk;
        if (!gizmo->editing) { gizmo->value[0] = prop.x; gizmo->value[1] = prop.y; gizmo->value[2] = prop.z; }
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(width), float(height));
    io.DisplayFramebufferScale = ImVec2(1, 1);
    float x, y, outputX, outputY;
    SDL_GetMouseState(&x, &y);
    if (SDL_GetMouseFocus() == gizmo->window && SDL_RenderCoordinatesFromWindow(gizmo->renderer, x, y, &outputX, &outputY))
        io.AddMousePosEvent(outputX, outputY);
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    ImGuizmo::Enable(eligible && !gizmo->cancel);
}
void prop_gizmo_view(MarcusPropGizmo *gizmo, const MarcusImage *image, const SDL_FRect *viewport)
{
    if (!gizmo->enabled || gizmo->cancel || gizmo->editing == 2 || !image ||
        image->document != gizmo->document || image->scene != gizmo->scene || image->frame != gizmo->frame) return;
    float view[16], projection[16];
    if (!camera_projection_matrices(&image->projection, view, projection)) return;
    auto rect = *viewport;
    if (image->authored_camera)
    {
        const float aspect = image->projection.aspect;
        if (rect.w / rect.h > aspect) { const auto w = rect.h * aspect; rect.x += (rect.w - w) / 2; rect.w = w; }
        else { const auto h = rect.w / aspect; rect.y += (rect.h - h) / 2; rect.h = h; }
    }
    float matrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, gizmo->value[0],gizmo->value[1],gizmo->value[2],1};
    auto draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), true);
    ImGuizmo::SetDrawlist(draw);
    ImGuizmo::SetRect(rect.x, rect.y, rect.w, rect.h);
    ImGuizmo::SetOrthographic(image->projection.orthographic != 0);
    ImGuizmo::AllowAxisFlip(false);
    ImGuizmo::SetTranslationAxisMinimumSize(24.f);
    ImGuizmo::SetTranslationPlaneMinimumSize(12.f);
    const float snap[3] = {1, 1, 1};
    const bool moved = ImGuizmo::Manipulate(view, projection, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, matrix,
                                           nullptr, ImGui::GetIO().KeyCtrl ? snap : nullptr);
    if (ImGuizmo::IsUsing() || moved) gizmo->Start(1);
    if (moved) std::copy(matrix + 12, matrix + 15, gizmo->value);
    gizmo->over = ImGuizmo::IsOver();
    if (gizmo->over && !gizmo->editing)
        ImGui::SetTooltip("Drag an axis or plane to move the prop. Ctrl snaps to 1 unit. Escape cancels.");
    if (gizmo->editing == 1 && !ImGuizmo::IsUsing()) gizmo->finish = true;
    draw->PopClipRect();
}
int prop_gizmo_properties(MarcusPropGizmo *gizmo, float x, float y, const SDL_Rect *clip)
{
    if (!gizmo->enabled || gizmo->cancel) return 0;
    const float top = std::max(y, float(clip->y)), bottom = std::min(y + 78, float(clip->y + clip->h));
    if (bottom <= top) return 1;
    gizmo->fieldRect = {x, top, 162, bottom - top};
    gizmo->fields = true;
    ImGui::SetNextWindowPos(ImVec2(x, top));
    ImGui::SetNextWindowSize(ImVec2(162, bottom - top));
    ImGui::Begin("Prop position", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginDisabled(gizmo->editing == 1);
    for (int axis = 0; axis < 3; ++axis)
    {
        ImGui::PushID(axis);
        ImGui::SetCursorScreenPos(ImVec2(x, y + axis * 26));
        ImGui::SetNextItemWidth(162);
        float value = gizmo->value[axis];
        const bool updated = ImGui::DragFloat("##position", &value, .1f, -32700, 32700, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemActivated()) gizmo->Start(2);
        if (updated && std::isfinite(value)) { gizmo->Start(2); gizmo->value[axis] = value; }
        if (ImGui::IsItemDeactivated() && gizmo->editing == 2) gizmo->finish = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to move; Ctrl+click to type. Escape cancels. Moves the whole prop track.");
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::End();
    return 1;
}
int prop_gizmo_update(MarcusPropGizmo *gizmo)
{
    int result = gizmo->changed;
    if (gizmo->editing)
    {
        // A rejected mouse sample must not reach the fixed-point player or
        // discard the valid part of a drag. Releasing commits the last good pose.
        if (!std::all_of(gizmo->value, gizmo->value + 3,
                [](float value) { return std::isfinite(value) && std::abs(value) <= 32700.f; }))
            std::copy(gizmo->applied, gizmo->applied + 3, gizmo->value);
        if (gizmo->cancel)
        {
            result = gizmo->Restore();
            gizmo->editing = 0;
            ImGuizmo::Enable(false);
        }
        else if (gizmo->finish)
        {
            const bool moved = std::abs(gizmo->value[0] - gizmo->original[0]) > .00002f ||
                std::abs(gizmo->value[1] - gizmo->original[1]) > .00002f ||
                std::abs(gizmo->value[2] - gizmo->original[2]) > .00002f;
            if (moved)
            {
                if (marcus_move_prop(gizmo->player, gizmo->object, gizmo->value[0], gizmo->value[1], gizmo->value[2])) result = 2;
                else { gizmo->StoreError(); gizmo->Restore(); result = -1; }
            }
            else result = gizmo->Restore();
            gizmo->editing = 0;
        }
        else if (!std::equal(gizmo->value, gizmo->value + 3, gizmo->applied))
        {
            if (marcus_preview_prop_position(gizmo->player, gizmo->object, gizmo->value[0], gizmo->value[1], gizmo->value[2]))
            {
                std::copy(gizmo->value, gizmo->value + 3, gizmo->applied);
                result = 1;
            }
            else { gizmo->StoreError(); gizmo->Restore(); gizmo->editing = 0; result = -1; ImGuizmo::Enable(false); }
        }
    }
    if (result < 0 && !gizmo->error[0]) gizmo->StoreError();
    gizmo->cancel = false;
    return result;
}
int prop_gizmo_active(const MarcusPropGizmo *gizmo) { return gizmo && gizmo->editing != 0; }
void prop_gizmo_end(MarcusPropGizmo *gizmo)
{
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), gizmo->renderer);
}

const char *prop_gizmo_error(const MarcusPropGizmo *gizmo) { return gizmo ? gizmo->error : "Prop controls are unavailable."; }
