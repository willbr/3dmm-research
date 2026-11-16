#include "text_dialog.h"
#include "storyboard.h"
#include "preview_worker.h"
#include "colour_picker.h"
#include <atomic>
#include <future>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <string>
#include <vector>

namespace
{
constexpr float dialogWidth = 1060, dialogHeight = 720;
constexpr const char *shapes[] = {"Normal", "Arch up", "Circle around Y", "Large middle", "Arch down",
    "Arch around Z", "Circle around Z", "Vertical", "Grow right", "Grow left"};
size_t Previous(const std::string &text, size_t at)
{
    if (at) --at;
    while (at && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) --at;
    return at;
}
size_t Next(const std::string &text, size_t at)
{
    if (at < text.size()) ++at;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) ++at;
    return at;
}
}
struct MarcusTextDialog
{
    bool open = false, selectAll = true;
    int font = 0, shape = 0, focus = 0, chosen = -1, object = -1;
    unsigned long document = 0;
    size_t cursor = 4;
    MarcusPlayer *player = nullptr;
    SDL_Window *window = nullptr;
    std::string text = "Text", error;
    std::vector<MarcusProp> fonts;
    std::vector<long> fontSources;
    std::vector<MarcusProp> materials;
    struct Swatch { int index; unsigned rgb; };
    std::vector<Swatch> swatches;
    int materialIndex = -1, dragField = -1;
    float fieldStart = 0, fieldValue = 0;
    MarcusTextMaterial appearance = {};
    float uv[5] = {}, uvOriginal[6] = {}, uvStart[3] = {};
    bool uvDirty = false, previewDirty = true, rotating = false;
    float yaw = 20, pitch = -10, zoom = 1, pointerX = 0, pointerY = 0;
    Uint64 lastPreview = 0;
    std::string resources, previewError;
    SDL_Texture *previewTexture = nullptr;
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::future<BrowserPreview> pending;
    ~MarcusTextDialog() { StopPreview(); SDL_DestroyTexture(previewTexture); }
    void StopPreview() { if (cancelled) *cancelled = true; if (pending.valid()) pending.wait(); pending = {}; }
    void Changed() { previewDirty = true; error.clear(); }
    void ReadUV()
    {
        std::copy(appearance.mapping, appearance.mapping + 6, uvOriginal);
        uv[0] = std::hypot(uvOriginal[0], uvOriginal[1]); uv[1] = std::hypot(uvOriginal[2], uvOriginal[3]);
        uv[2] = std::atan2(uvOriginal[1], uvOriginal[0]) * 57.295779513f;
        uv[3] = uvOriginal[4]; uv[4] = uvOriginal[5];
        std::copy(uv, uv + 3, uvStart); uvDirty = false;
    }
    MarcusText Value() const
    {
        MarcusText value = {};
        SDL_strlcpy(value.text, text.c_str(), sizeof(value.text)); value.font = fonts[font].chunk;
        value.font_source = fontSources[font]; value.shape = shape; value.appearance = appearance;
        if (uvDirty)
        {
            const float angle = (uv[2] - uvStart[2]) * .01745329252f, c = std::cos(angle), s = std::sin(angle);
            for (int row = 0; row < 2; ++row)
            {
                const float ratio = uvStart[row] > .00001f ? uv[row] / uvStart[row] : 1;
                value.appearance.mapping[row * 2] = (uvOriginal[row * 2] * c - uvOriginal[row * 2 + 1] * s) * ratio;
                value.appearance.mapping[row * 2 + 1] = (uvOriginal[row * 2] * s + uvOriginal[row * 2 + 1] * c) * ratio;
            }
            value.appearance.mapping[4] = uv[3]; value.appearance.mapping[5] = uv[4];
        }
        return value;
    }
    void ChooseMaterial(int index)
    {
        if (index < 0 || index >= int(materials.size())) return;
        if (!marcus_text_material_preset(player, materials[index].chunk, &appearance)) { error = marcus_error(player); return; }
        materialIndex = index; ReadUV(); Changed();
    }
    void StepMaterial(int delta)
    {
        if (materials.empty()) return;
        ChooseMaterial(materialIndex < 0 ? (delta > 0 ? 0 : int(materials.size()) - 1) :
            (materialIndex + int(materials.size()) + delta) % int(materials.size()));
    }
    struct Field { const char *label; float *value; float step, low, high; bool enabled; };
    std::vector<Field> Fields()
    {
        return {{"Ambient", &appearance.ambient, .05f, 0, 1, true}, {"Diffuse", &appearance.diffuse, .05f, 0, 1, true},
            {"Specular", &appearance.specular, .05f, 0, 1, true}, {"Shininess", &appearance.shininess, 5, 0, 1000, true},
            {"U scale", &uv[0], .1f, .01f, 32, appearance.textured != 0}, {"V scale", &uv[1], .1f, .01f, 32, appearance.textured != 0},
            {"Rotation", &uv[2], 15, -180, 180, appearance.textured != 0}, {"U offset", &uv[3], .1f, -128, 128, appearance.textured != 0},
            {"V offset", &uv[4], .1f, -128, 128, appearance.textured != 0}};
    }
    void SetField(int field, float value)
    {
        auto fields = Fields(); auto &f = fields[field];
        if (!f.enabled) return;
        *f.value = std::clamp(value, f.low, f.high); if (field >= 4) uvDirty = true; Changed();
    }
    SDL_FRect PreviewRect(float x, float y) const { return {x + 620, y + 164, 416, 312}; }
    void Preview(MarcusUI *ui)
    {
        if (pending.valid() && pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                const auto result = pending.get(); previewError = result.error;
                if (!result.rgba.empty())
                {
                    auto surface = SDL_CreateSurfaceFrom(result.width, result.height, SDL_PIXELFORMAT_RGBA32,
                        const_cast<unsigned char *>(result.rgba.data()), result.width * 4);
                    auto texture = surface ? SDL_CreateTextureFromSurface(ui->renderer, surface) : nullptr;
                    SDL_DestroySurface(surface);
                    if (texture) { SDL_DestroyTexture(previewTexture); previewTexture = texture; SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR); }
                }
            }
            catch (const std::exception &e) { previewError = e.what(); }
        }
        const auto now = SDL_GetTicks();
        if (!previewDirty || now - lastPreview < 180) return;
        previewDirty = false; lastPreview = now; StopPreview();
        auto file = MarcusPrepareTextPreview(player, object, Value());
        if (!file) { previewError = marcus_error(player); return; }
        cancelled = std::make_shared<std::atomic<bool>>(false); const auto cancel = cancelled;
        const auto resource = resources; std::ostringstream camera; camera << yaw << ' ' << pitch << ' ' << zoom;
        const auto view = camera.str(); previewError.clear();
        try { pending = std::async(std::launch::async, [file, cancel, resource, view] {
            return RunPreviewWorker(file->path.u8string(), resource, [cancel] { return cancel->load(); }, view, false, {}, false, false, true);
        }); } catch (const std::exception &e) { previewError = e.what(); }
    }
    void Close() { StopPreview(); open = false; rotating = false; dragField = -1; if (window) SDL_StopTextInput(window); }
    void Focus(int field)
    {
        focus = field;
        if (!focus) SDL_StartTextInput(window); else SDL_StopTextInput(window);
    }
    void Insert(const char *input)
    {
        auto next = text;
        const size_t at = selectAll ? 0 : cursor;
        if (selectAll) next.clear();
        next.insert(at, input);
        if (SDL_utf8strlen(next.c_str()) > 50) { error = "Use at most 50 characters."; return; }
        text = std::move(next); cursor = at + SDL_strlen(input); selectAll = false; Changed();
    }
    void Add()
    {
        if (object >= 0)
        {
            if (document != marcus_document(player)) { error = "The movie changed. Close and reopen the text editor."; return; }
            const auto value = Value();
            if (!marcus_set_text(player, object, &value)) { error = marcus_error(player); return; }
            chosen = marcus_document(player) != document ? object : -1;
            Close(); return;
        }
        const auto value = Value();
        chosen = marcus_add_text_options(player, &value);
        if (chosen >= 0) Close(); else error = marcus_error(player);
    }
};
extern "C" {
MarcusTextDialog *text_dialog_create(void) { try { return new MarcusTextDialog; } catch (...) { return nullptr; } }
void text_dialog_destroy(MarcusTextDialog *dialog) { if (dialog && dialog->open) dialog->Close(); delete dialog; }
int text_dialog_visible(const MarcusTextDialog *dialog) { return dialog && dialog->open; }
static int Show(MarcusTextDialog *dialog, MarcusPlayer *player, SDL_Window *window, const char *content, const char *resources, int object)
{
    if (!dialog || !window) return 0;
    MarcusText current = {};
    if (object >= 0 && !marcus_text(player, object, &current)) return 0;
    const int count = marcus_font_count(player, content);
    if (count <= 0 && object < 0) return 0;
    dialog->fonts.clear();
    dialog->fontSources.clear();
    for (int i = 0; i < count; ++i)
    {
        MarcusProp font = {};
        if (marcus_font(player, i, &font)) { dialog->fonts.push_back(font); dialog->fontSources.push_back(-1); }
    }
    if (object >= 0)
    {
        dialog->text = current.text; dialog->shape = current.shape;
        const auto found = std::find_if(dialog->fonts.begin(), dialog->fonts.end(), [&](const auto &font) {
            return current.font_source == -1 && font.chunk == current.font;
        });
        if (found != dialog->fonts.end()) dialog->font = int(found - dialog->fonts.begin());
        else
        {
            MarcusProp font = {}; font.chunk = current.font;
            SDL_strlcpy(font.name, "Current font", sizeof(font.name));
            dialog->font = int(dialog->fonts.size()); dialog->fonts.push_back(font); dialog->fontSources.push_back(current.font_source);
        }
    }
    if (dialog->fonts.empty()) return 0;
    dialog->font = std::clamp(dialog->font, 0, int(dialog->fonts.size()) - 1);
    dialog->StopPreview(); SDL_DestroyTexture(dialog->previewTexture); dialog->previewTexture = nullptr;
    dialog->chosen = -1; dialog->player = player; dialog->window = window;
    dialog->appearance = current.appearance;
    if (object < 0)
    {
        dialog->appearance = {}; auto &m = dialog->appearance;
        m.enabled = 1; m.index_base = 15; m.index_range = 15; m.ambient = .1f; m.diffuse = m.specular = .6f; m.shininess = 50;
        m.mapping[0] = m.mapping[3] = 1;
    }
    dialog->materialIndex = -1; dialog->materials.clear(); dialog->swatches.clear();
    MarcusBoardPalette palette = {}; MarcusGetStoryboardPalette(player, palette);
    const int materialCount = marcus_material_count(player, content);
    for (int i = 0; i < materialCount; ++i)
    {
        MarcusProp material = {}; if (!marcus_material(player, i, &material)) continue;
        dialog->materials.push_back(material);
        MarcusTextMaterial value = {};
        if (marcus_text_material_preset(player, material.chunk, &value) && !value.textured)
        {
            const unsigned rgb = palette[std::min(255U, value.index_base + value.index_range / 2)];
            if (std::none_of(dialog->swatches.begin(), dialog->swatches.end(), [&](const auto &swatch) { return swatch.rgb == rgb; }))
                dialog->swatches.push_back({int(dialog->materials.size()) - 1, rgb});
        }
    }
    std::sort(dialog->swatches.begin(), dialog->swatches.end(), [](const auto &a, const auto &b) { return MarcusToOkHSL(a.rgb)[0] < MarcusToOkHSL(b.rgb)[0]; });
    if (dialog->swatches.size() > 12)
    {
        auto all = dialog->swatches; dialog->swatches.clear();
        for (int i = 0; i < 12; ++i) dialog->swatches.push_back(all[i * (all.size() - 1) / 11]);
    }
    dialog->ReadUV(); dialog->previewDirty = true; dialog->previewError.clear(); dialog->lastPreview = 0;
    dialog->dragField = -1; dialog->rotating = false; dialog->yaw = 20; dialog->pitch = -10; dialog->zoom = 1;
    dialog->resources = resources;
    dialog->object = object; dialog->document = marcus_document(player);
    dialog->error.clear(); dialog->open = dialog->selectAll = true;
    dialog->cursor = dialog->text.size(); dialog->Focus(0);
    return 1;
}
int text_dialog_show(MarcusTextDialog *dialog, MarcusPlayer *player, SDL_Window *window, const char *content, const char *resources)
{ return Show(dialog, player, window, content, resources, -1); }
int text_dialog_edit(MarcusTextDialog *dialog, MarcusPlayer *player, SDL_Window *window, const char *content, const char *resources, int object)
{ return object >= 0 && Show(dialog, player, window, content, resources, object); }
int text_dialog_event(MarcusTextDialog *d, const SDL_Event *event, int width, int height)
{
    if (!text_dialog_visible(d)) return 0;
    const float x = (width - dialogWidth) / 2, y = (height - dialogHeight) / 2;
    if (event->type == SDL_EVENT_TEXT_INPUT && !d->focus) d->Insert(event->text.text);
    else if (event->type == SDL_EVENT_KEY_DOWN)
    {
        const auto key = event->key.key;
        const bool command = event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI);
        if (key == SDLK_ESCAPE) d->Close();
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) { if (!event->key.repeat) d->Add(); }
        else if (key == SDLK_TAB) d->Focus((d->focus + ((event->key.mod & SDL_KMOD_SHIFT) ? 13 : 1)) % 14);
        else if (d->focus == 13)
        {
            if (key == SDLK_LEFT) d->yaw -= 15;
            if (key == SDLK_RIGHT) d->yaw += 15;
            if (key == SDLK_UP) d->pitch = std::max(-89.f, d->pitch - 15);
            if (key == SDLK_DOWN) d->pitch = std::min(89.f, d->pitch + 15);
            d->Changed();
        }
        else if (d->focus >= 4)
        {
            const auto fields = d->Fields(); const int index = d->focus - 4;
            if (key == SDLK_LEFT || key == SDLK_DOWN) d->SetField(index, *fields[index].value - fields[index].step);
            if (key == SDLK_RIGHT || key == SDLK_UP) d->SetField(index, *fields[index].value + fields[index].step);
        }
        else if (d->focus == 3)
        {
            if (key == SDLK_LEFT || key == SDLK_UP) d->StepMaterial(-1);
            if (key == SDLK_RIGHT || key == SDLK_DOWN) d->StepMaterial(1);
        }
        else if (d->focus)
        {
            int &value = d->focus == 1 ? d->font : d->shape;
            const int count = d->focus == 1 ? int(d->fonts.size()) : 10;
            if (key == SDLK_LEFT || key == SDLK_UP) value = (value + count - 1) % count;
            if (key == SDLK_RIGHT || key == SDLK_DOWN) value = (value + 1) % count;
            d->Changed();
        }
        else if (command && key == SDLK_A) d->selectAll = true;
        else if (command && (key == SDLK_C || key == SDLK_X) && d->selectAll)
        {
            SDL_SetClipboardText(d->text.c_str());
            if (key == SDLK_X) { d->text.clear(); d->cursor = 0; d->selectAll = false; d->Changed(); }
        }
        else if (command && key == SDLK_V)
        {
            char *clipboard = SDL_GetClipboardText();
            if (clipboard) { d->Insert(clipboard); SDL_free(clipboard); }
        }
        else if (key == SDLK_BACKSPACE || key == SDLK_DELETE)
        {
            if (d->selectAll) { d->text.clear(); d->cursor = 0; }
            else if (key == SDLK_BACKSPACE && d->cursor)
            {
                const auto at = Previous(d->text, d->cursor);
                d->text.erase(at, d->cursor - at); d->cursor = at;
            }
            else if (key == SDLK_DELETE && d->cursor < d->text.size()) d->text.erase(d->cursor, Next(d->text, d->cursor) - d->cursor);
            d->selectAll = false; d->Changed();
        }
        else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_HOME || key == SDLK_END)
        {
            if (key == SDLK_HOME) d->cursor = 0;
            else if (key == SDLK_END) d->cursor = d->text.size();
            else if (d->selectAll) d->cursor = key == SDLK_LEFT ? 0 : d->text.size();
            else d->cursor = key == SDLK_LEFT ? Previous(d->text, d->cursor) : Next(d->text, d->cursor);
            d->selectAll = false;
        }
    }
    else if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        if (d->dragField >= 0)
        {
            const auto fields = d->Fields(); const float scale = event->motion.state & SDL_BUTTON_LMASK ? .1f : 0;
            d->SetField(d->dragField, d->fieldValue + (event->motion.x - d->fieldStart) * fields[d->dragField].step * scale);
        }
        if (d->rotating)
        {
            d->yaw += (event->motion.x - d->pointerX) * .6f;
            d->pitch = std::clamp(d->pitch + (event->motion.y - d->pointerY) * .6f, -89.f, 89.f);
            d->pointerX = event->motion.x; d->pointerY = event->motion.y; d->Changed();
        }
    }
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_WINDOW_FOCUS_LOST)
    { d->dragField = -1; d->rotating = false; }
    else if (event->type == SDL_EVENT_MOUSE_WHEEL)
    {
        const auto r = d->PreviewRect(x, y);
        if (ui_inside(event->wheel.mouse_x, event->wheel.mouse_y, r.x, r.y, r.w, r.h))
        {
            d->zoom = std::clamp(d->zoom * std::pow(1.15f, event->wheel.y * (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1)), .25f, 4.f);
            d->Changed();
        }
    }
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        const float px = event->button.x, py = event->button.y;
        const auto preview = d->PreviewRect(x, y);
        if (ui_inside(px, py, x + 24, y + 92, 1012, 38)) { d->Focus(0); d->cursor = d->text.size(); d->selectAll = true; }
        else if (ui_inside(px, py, x + 808, y + 666, 92, 30) || ui_inside(px, py, x + 1004, y + 18, 32, 28)) d->Close();
        else if (ui_inside(px, py, x + 912, y + 666, 124, 30)) d->Add();
        else if (ui_inside(px, py, preview.x, preview.y, preview.w, preview.h))
        { d->Focus(13); d->rotating = true; d->pointerX = px; d->pointerY = py; }
        else
        {
            for (int field = 1; field < 4; ++field)
            {
                const float top = y + 164 + (field - 1) * 64;
                if (!ui_inside(px, py, x + 132, top, 438, 32)) continue;
                d->Focus(field);
                if (field == 3) { if (px < x + 170) d->StepMaterial(-1); else if (px >= x + 532) d->StepMaterial(1); }
                else
                {
                    int &value = field == 1 ? d->font : d->shape; const int count = field == 1 ? int(d->fonts.size()) : 10;
                    if (px < x + 170) value = (value + count - 1) % count;
                    else if (px >= x + 532) value = (value + 1) % count;
                    d->Changed();
                }
            }
            for (int i = 0; i < int(d->swatches.size()); ++i)
                if (ui_inside(px, py, x + 24 + i * 46, y + 340, 40, 24)) { d->Focus(3); d->ChooseMaterial(d->swatches[i].index); }
            auto fields = d->Fields();
            for (int i = 0; i < int(fields.size()); ++i)
            {
                const float left = x + (i < 4 ? 24 : 320), top = y + 404 + (i < 4 ? i : i - 4) * 36;
                if (!fields[i].enabled || !ui_inside(px, py, left + 104, top, 160, 28)) continue;
                d->Focus(i + 4);
                if (px >= left + 228) d->SetField(i, *fields[i].value + fields[i].step);
                else if (px >= left + 192) d->SetField(i, *fields[i].value - fields[i].step);
                else { d->dragField = i; d->fieldStart = px; d->fieldValue = *fields[i].value; }
            }
            for (int i = 0; i < 5; ++i)
                if (ui_inside(px, py, x + 620 + i * 84, y + 492, 80, 28))
                {
                    if (i < 2) d->yaw += i ? 30 : -30;
                    else if (i < 4) d->zoom = std::clamp(d->zoom * (i == 2 ? .8f : 1.25f), .25f, 4.f);
                    else { d->yaw = 20; d->pitch = -10; d->zoom = 1; }
                    d->Changed();
                }
        }
    }
    return event->type != SDL_EVENT_QUIT && event->type != SDL_EVENT_WINDOW_CLOSE_REQUESTED;
}
void text_dialog_draw(MarcusTextDialog *d, MarcusUI *ui, int width, int height)
{
    if (!text_dialog_visible(d)) return;
    d->Preview(ui);
    const float x = (width - dialogWidth) / 2, y = (height - dialogHeight) / 2;
    ui_fill(ui, 0, 0, float(width), float(height), 0x121519);
    ui_fill(ui, x - 1, y - 1, dialogWidth + 2, dialogHeight + 2, 0x5b6570);
    ui_fill(ui, x, y, dialogWidth, dialogHeight, 0x25282d);
    ui_heading(ui, x + 24, y + 22, d->object >= 0 ? "EDIT 3D TEXT" : "NEW 3D TEXT", 0x78cbc4);
    ui_button(ui, x + 1004, y + 18, 32, "x", 0, 1);
    char count[128]; SDL_snprintf(count, sizeof(count), "Text / %d of 50 characters", int(SDL_utf8strlen(d->text.c_str())));
    ui_text(ui, x + 24, y + 66, count, 0xdde0e5);
    ui_fill(ui, x + 23, y + 91, 1014, 40, !d->focus ? 0x78cbc4 : 0x424952);
    ui_fill(ui, x + 24, y + 92, 1012, 38, !d->focus && d->selectAll ? 0x375e64 : 0x1c1e22);
    std::string display = d->text;
    if (!d->focus && !d->selectAll && SDL_GetTicks() % 1000 < 500) display.insert(d->cursor, "|");
    ui_label(ui, x + 34, y + 104, 992, display.c_str(), 0xdde0e5);
    for (int field = 1; field < 4; ++field)
    {
        const float top = y + 164 + (field - 1) * 64;
        ui_text(ui, x + 24, top + 8, field == 1 ? "3D font" : field == 2 ? "Shape" : "Material", 0xdde0e5);
        ui_button_sized(ui, x + 132, top, 38, 32, "<", d->focus == field, 1);
        ui_fill(ui, x + 178, top, 346, 32, 0x1c1e22);
        const char *name = field == 1 ? d->fonts[d->font].name : field == 2 ? shapes[d->shape] :
            d->materialIndex >= 0 ? d->materials[d->materialIndex].name : d->object >= 0 ? "Current material" : "Default material";
        ui_label(ui, x + 190, top + 8, 322, name, 0xdde0e5);
        ui_button_sized(ui, x + 532, top, 38, 32, ">", d->focus == field, 1);
    }
    for (int i = 0; i < int(d->swatches.size()); ++i)
    {
        ui_fill(ui, x + 22 + i * 46, y + 338, 44, 28, d->materialIndex == d->swatches[i].index ? 0x78cbc4 : 0x424952);
        ui_fill(ui, x + 24 + i * 46, y + 340, 40, 24, d->swatches[i].rgb);
        if (ui_inside(ui->mouse_x, ui->mouse_y, x + 24 + i * 46, y + 340, 40, 24)) ui->hint = d->materials[d->swatches[i].index].name;
    }
    ui_heading(ui, x + 24, y + 378, "LIGHTING", 0x969da8);
    ui_heading(ui, x + 320, y + 378, "TEXTURE PLACEMENT", 0x969da8);
    const auto fields = d->Fields();
    for (int i = 0; i < int(fields.size()); ++i)
    {
        const auto &f = fields[i]; const float left = x + (i < 4 ? 24 : 320), top = y + 404 + (i < 4 ? i : i - 4) * 36;
        ui_text(ui, left, top + 7, f.label, f.enabled ? 0xdde0e5 : 0x737b86);
        ui_fill(ui, left + 104, top, 80, 28, d->focus == i + 4 ? 0x375e64 : 0x1c1e22);
        SDL_snprintf(count, sizeof(count), "%.2f", *f.value); ui_label(ui, left + 110, top + 7, 68, count, f.enabled ? 0xdde0e5 : 0x737b86);
        ui_button_sized(ui, left + 192, top, 28, 28, "-", 0, f.enabled);
        ui_button_sized(ui, left + 228, top, 28, 28, "+", 0, f.enabled);
    }
    const auto preview = d->PreviewRect(x, y);
    ui_fill(ui, preview.x - 1, preview.y - 1, preview.w + 2, preview.h + 2, 0x424952);
    ui_fill(ui, preview.x, preview.y, preview.w, preview.h, 0x13161a);
    if (d->previewTexture) SDL_RenderTexture(ui->renderer, d->previewTexture, nullptr, &preview);
    else ui_text(ui, preview.x + 126, preview.y + 146, d->previewError.empty() ? "Loading preview..." : "Preview unavailable", 0x969da8);
    const char *buttons[] = {"Rotate <", "Rotate >", "Zoom -", "Zoom +", "Reset"};
    for (int i = 0; i < 5; ++i) ui_button_sized(ui, x + 620 + i * 84, y + 492, 80, 28, buttons[i], 0, 1);
    ui_text(ui, x + 620, y + 538, "Drag to rotate / Wheel to zoom", 0x969da8);
    ui_text(ui, x + 620, y + 566, "Preview pose does not change the movie.", 0x969da8);
    ui_label(ui, x + 620, y + 594, 416, d->previewError.c_str(), 0xe4a1a1);
    ui_text(ui, x + 24, y + 614, d->object >= 0 ? "Keeps placement and animation. Appearance applies to the whole text." :
        "Inserted at this frame using the camera's next insertion point.", 0x969da8);
    ui_label(ui, x + 24, y + 642, 1012, d->error.c_str(), 0xe4a1a1);
    ui_text(ui, x + 24, y + 676, d->object >= 0 ? "Tab to switch / Drag values or use - + / Enter to save" :
        "Tab to switch / Drag values or use - + / Enter to add", 0x969da8);
    ui_button_sized(ui, x + 808, y + 666, 92, 30, "Cancel", 0, 1);
    ui_button_sized(ui, x + 912, y + 666, 124, 30, d->object >= 0 ? "Save changes" : "Add 3D text", 1, 1);
}
int text_dialog_take(MarcusTextDialog *d, int *edited)
{
    if (!d) return -1;
    if (edited) *edited = d->object >= 0 && d->chosen >= 0;
    const int chosen = d->chosen; d->chosen = -1; return chosen;
}
}
