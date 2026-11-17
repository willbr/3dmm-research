#include "prop_gizmo.h"
#include "camera_guide.h"
#include "transform_gizmo.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "ImGuizmo.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
void Multiply(const float a[9], const float b[9], float out[9])
{
    float result[9] = {};
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) for (int k=0;k<3;++k) result[i*3+j] += a[i*3+k]*b[k*3+j];
    std::copy(result,result+9,out);
}
bool Inverse(const float a[9], float out[9])
{
    const float det = a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
    if (!std::isfinite(det) || std::abs(det)<.001f) return false;
    const float v[9] = {a[4]*a[8]-a[5]*a[7],a[2]*a[7]-a[1]*a[8],a[1]*a[5]-a[2]*a[4],
        a[5]*a[6]-a[3]*a[8],a[0]*a[8]-a[2]*a[6],a[2]*a[3]-a[0]*a[5],
        a[3]*a[7]-a[4]*a[6],a[1]*a[6]-a[0]*a[7],a[0]*a[4]-a[1]*a[3]};
    for (int i=0;i<9;++i) out[i]=v[i]/det;
    return true;
}
void Matrix(const float rotation[9], const float scale[3], const float position[3], float out[16])
{
    std::fill(out,out+16,0.f); out[15]=1;
    for (int i=0;i<9;++i) out[(i/3)*4+i%3]=rotation[i]*scale[i/3];
    std::copy(position,position+3,out+12);
}
void Angles(const MarcusObjectShape &shape, float out[3])
{
    const float one[3]={1,1,1},zero[3]={}; float matrix[16],position[3],scale[3];
    Matrix(shape.rotation,one,zero,matrix);
    ImGuizmo::DecomposeMatrixToComponents(matrix,position,out,scale);
}
}

struct MarcusPropGizmo
{
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    ImGuiContext *context = nullptr;
    MarcusPlayer *player = nullptr;
    int object = -1, scene = -1, editing = 0, changed = 0;
    int mode = 0;
    long frame = 0;
    unsigned long document = 0, chunk = 0;
    bool enabled = false, cancel = false, finish = false, platform = false, render = false;
    bool over = false, fields = false;
    SDL_FRect fieldRect = {};
    SDL_FRect controlRect = {};
    float original[3] = {}, value[3] = {}, applied[3] = {};
    float position[3] = {}, left[9] = {}, right[9] = {};
    MarcusObjectShape shape = {}, originalShape = {}, appliedShape = {};
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
        originalShape = appliedShape = shape;
        editing = kind;
    }
    int Restore()
    {
        if (!editing || !SameContext()) return 0;
        return (mode ? marcus_preview_object_shape(player, object, &originalShape) :
            marcus_preview_object_position(player, object, original[0], original[1], original[2])) ? 1 : -1;
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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
        const auto &c = gizmo->controlRect;
        const bool inControls = x >= c.x && x < c.x+c.w && y >= c.y && y < c.y+c.h;
        const bool left = event->type == SDL_EVENT_MOUSE_MOTION || event->button.button == SDL_BUTTON_LEFT;
        return gizmo->editing || (left && (gizmo->over || inFields || inControls));
    }
    return 0;
}
void prop_gizmo_begin(MarcusPropGizmo *gizmo, MarcusPlayer *player, int object, int enabled, int width, int height)
{
    ImGui::SetCurrentContext(gizmo->context);
    ImGuizmo::SetID(MarcusGizmo::Object);
    gizmo->changed = 0;
    gizmo->error[0] = 0;
    MarcusObject prop;
    MarcusInfo info;
    const bool eligible = enabled && player && marcus_object(player, object, &prop) &&
        prop.visible && marcus_info(player, &info);
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
        gizmo->position[0]=prop.x; gizmo->position[1]=prop.y; gizmo->position[2]=prop.z;
        if (!gizmo->editing)
        {
            marcus_object_shape(player,object,&gizmo->shape,gizmo->left,gizmo->right);
            if (gizmo->mode==1) Angles(gizmo->shape,gizmo->value);
            else if (gizmo->mode==2) for (int i=0;i<3;++i) gizmo->value[i]=gizmo->shape.scale[i]*100;
            else std::copy(gizmo->position,gizmo->position+3,gizmo->value);
        }
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
    if (!image->projection.valid) return;
    auto rect = *viewport;
    if (image->authored_camera)
    {
        const float aspect = image->projection.aspect;
        if (rect.w / rect.h > aspect) { const auto w = rect.h * aspect; rect.x += (rect.w - w) / 2; rect.w = w; }
        else { const auto h = rect.w / aspect; rect.y += (rect.h - h) / 2; rect.h = h; }
    }
    gizmo->controlRect={rect.x+8,rect.y+8,196,28};
    const int previousMode = gizmo->mode;
    gizmo->over = MarcusGizmo::Controls("Object tools", gizmo->controlRect, gizmo->mode, 3, gizmo->editing != 0);
    if (previousMode != gizmo->mode)
    {
        if (gizmo->mode==1) Angles(gizmo->shape,gizmo->value);
        else if (gizmo->mode==2) for (int a=0;a<3;++a) gizmo->value[a]=gizmo->shape.scale[a]*100;
        else std::copy(gizmo->position,gizmo->position+3,gizmo->value);
    }
    const float one[3]={1,1,1};
    float rotation[9],matrix[16];
    Multiply(gizmo->left,gizmo->shape.rotation,rotation); Multiply(rotation,gizmo->right,rotation);
    Matrix(rotation,gizmo->mode ? gizmo->shape.scale : one,gizmo->mode ? gizmo->position : gizmo->value,matrix);
    const auto manipulation = MarcusGizmo::Manipulate(MarcusGizmo::Object, image->projection, rect, gizmo->mode, matrix);
    if (manipulation.active || manipulation.changed) gizmo->Start(1);
    if (manipulation.changed)
    {
        if (!gizmo->mode) std::copy(matrix + 12, matrix + 15, gizmo->value);
        else
        {
            float p[3],angles[3],scale[3],unit[16],inverseLeft[9],inverseRight[9],basis[9];
            ImGuizmo::DecomposeMatrixToComponents(matrix,p,angles,scale);
            if (gizmo->mode==1 && Inverse(gizmo->left,inverseLeft) && Inverse(gizmo->right,inverseRight))
            {
                const float zero[3]={};
                ImGuizmo::RecomposeMatrixFromComponents(zero,angles,one,unit);
                for (int i=0;i<9;++i) basis[i]=unit[(i/3)*4+i%3];
                Multiply(inverseLeft,basis,basis); Multiply(basis,inverseRight,gizmo->shape.rotation);
                Angles(gizmo->shape,gizmo->value);
            }
            else if (gizmo->mode==2)
                for (int i=0;i<3;++i) gizmo->value[i]=scale[i]*100;
        }
    }
    gizmo->over |= manipulation.over;
    if (gizmo->over && !gizmo->editing)
        ImGui::SetTooltip(gizmo->mode==1 ? "Drag a ring to rotate. Ctrl snaps to 15 degrees. Escape cancels." : gizmo->mode==2 ?
            "Drag an axis to resize, or the centre for uniform size. Ctrl snaps to 10%%. Escape cancels." :
            "Drag an axis or plane to move the object. Ctrl snaps to 1 unit. Escape cancels.");
    if (gizmo->editing == 1 && !manipulation.active) gizmo->finish = true;
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
    ImGui::Begin("Object position", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginDisabled(gizmo->editing == 1);
    for (int axis = 0; axis < 3; ++axis)
    {
        ImGui::PushID(axis);
        ImGui::SetCursorScreenPos(ImVec2(x, y + axis * 26));
        ImGui::SetNextItemWidth(162);
        float value = gizmo->value[axis];
        const bool updated = ImGui::DragFloat("##position", &value, gizmo->mode ? .5f : .1f,
            gizmo->mode==2 ? 1.f : gizmo->mode==1 ? -36000.f : -32700.f,
            gizmo->mode==2 ? 1000.f : gizmo->mode==1 ? 36000.f : 32700.f,
            gizmo->mode==2 ? "%.1f %%" : gizmo->mode==1 ? "%.2f deg" : "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemActivated()) gizmo->Start(2);
        if (updated && std::isfinite(value)) { gizmo->Start(2); gizmo->value[axis] = value; }
        if (ImGui::IsItemDeactivated() && gizmo->editing == 2) gizmo->finish = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to adjust; Ctrl+click to type. Escape cancels. Applies to the whole object track.");
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::End();
    return 1;
}
int prop_gizmo_update(MarcusPropGizmo *gizmo)
{
    ImGuizmo::SetID(MarcusGizmo::Object);
    int result = gizmo->changed;
    if (gizmo->editing)
    {
        // Saturate finite coordinates at the world limits. Overshooting must
        // stop at the boundary instead of rejecting a whole axis/plane sample.
        // Non-finite samples retain the last valid coordinate.
        for (int axis = 0; axis < 3; ++axis)
            gizmo->value[axis] = std::isfinite(gizmo->value[axis])
                ? std::clamp(gizmo->value[axis], gizmo->mode==2 ? 1.f : gizmo->mode==1 ? -36000.f : -32700.f,
                    gizmo->mode==2 ? 1000.f : gizmo->mode==1 ? 36000.f : 32700.f) : gizmo->applied[axis];
        if (gizmo->mode==2) for (int i=0;i<3;++i) gizmo->shape.scale[i]=gizmo->value[i]/100;
        if (gizmo->mode==1 && gizmo->editing==2)
        {
            const float zero[3]={},one[3]={1,1,1}; float matrix[16];
            ImGuizmo::RecomposeMatrixFromComponents(zero,gizmo->value,one,matrix);
            for (int i=0;i<9;++i) gizmo->shape.rotation[i]=matrix[(i/3)*4+i%3];
        }
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
                if (gizmo->mode ? marcus_set_object_shape(gizmo->player,gizmo->object,&gizmo->shape) :
                    marcus_move_object(gizmo->player, gizmo->object, gizmo->value[0], gizmo->value[1], gizmo->value[2])) result = 2;
                else { gizmo->StoreError(); gizmo->Restore(); result = -1; }
            }
            else result = gizmo->Restore();
            gizmo->editing = 0;
        }
        else if (!std::equal(gizmo->value, gizmo->value + 3, gizmo->applied))
        {
            if (gizmo->mode ? marcus_preview_object_shape(gizmo->player,gizmo->object,&gizmo->shape) :
                marcus_preview_object_position(gizmo->player, gizmo->object, gizmo->value[0], gizmo->value[1], gizmo->value[2]))
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
int prop_gizmo_mode(const MarcusPropGizmo *gizmo) { return gizmo ? gizmo->mode : 0; }
void prop_gizmo_end(MarcusPropGizmo *gizmo)
{
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), gizmo->renderer);
}

const char *prop_gizmo_error(const MarcusPropGizmo *gizmo) { return gizmo ? gizmo->error : "Object controls are unavailable."; }
