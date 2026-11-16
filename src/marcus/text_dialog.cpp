#include "text_dialog.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
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
    int font = 0, shape = 0, focus = 0, chosen = -1;
    size_t cursor = 4;
    MarcusPlayer *player = nullptr;
    SDL_Window *window = nullptr;
    std::string text = "Text", error;
    std::vector<MarcusProp> fonts;
    void Close() { open = false; if (window) SDL_StopTextInput(window); }
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
        text = std::move(next); cursor = at + SDL_strlen(input); selectAll = false; error.clear();
    }
    void Add()
    {
        chosen = marcus_add_text(player, text.c_str(), fonts[font].chunk, shape);
        if (chosen >= 0) Close(); else error = marcus_error(player);
    }
};
extern "C" {
MarcusTextDialog *text_dialog_create(void) { try { return new MarcusTextDialog; } catch (...) { return nullptr; } }
void text_dialog_destroy(MarcusTextDialog *dialog) { if (dialog && dialog->open) dialog->Close(); delete dialog; }
int text_dialog_visible(const MarcusTextDialog *dialog) { return dialog && dialog->open; }
int text_dialog_show(MarcusTextDialog *dialog, MarcusPlayer *player, SDL_Window *window, const char *content)
{
    if (!dialog || !window) return 0;
    const int count = marcus_font_count(player, content);
    if (count <= 0) return 0;
    dialog->fonts.clear();
    for (int i = 0; i < count; ++i)
    {
        MarcusProp font = {};
        if (marcus_font(player, i, &font)) dialog->fonts.push_back(font);
    }
    if (dialog->fonts.empty()) return 0;
    dialog->font = std::clamp(dialog->font, 0, int(dialog->fonts.size()) - 1);
    dialog->chosen = -1; dialog->player = player; dialog->window = window;
    dialog->error.clear(); dialog->open = dialog->selectAll = true;
    dialog->cursor = dialog->text.size(); dialog->Focus(0);
    return 1;
}
int text_dialog_event(MarcusTextDialog *d, const SDL_Event *event, int width, int height)
{
    if (!text_dialog_visible(d)) return 0;
    const float x = (width - 760) / 2.f, y = (height - 420) / 2.f;
    if (event->type == SDL_EVENT_TEXT_INPUT && !d->focus) d->Insert(event->text.text);
    else if (event->type == SDL_EVENT_KEY_DOWN)
    {
        const auto key = event->key.key;
        const bool command = event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI);
        if (key == SDLK_ESCAPE) d->Close();
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) { if (!event->key.repeat) d->Add(); }
        else if (key == SDLK_TAB) d->Focus((d->focus + ((event->key.mod & SDL_KMOD_SHIFT) ? 2 : 1)) % 3);
        else if (d->focus)
        {
            int &value = d->focus == 1 ? d->font : d->shape;
            const int count = d->focus == 1 ? int(d->fonts.size()) : 10;
            if (key == SDLK_LEFT || key == SDLK_UP) value = (value + count - 1) % count;
            if (key == SDLK_RIGHT || key == SDLK_DOWN) value = (value + 1) % count;
        }
        else if (command && key == SDLK_A) d->selectAll = true;
        else if (command && (key == SDLK_C || key == SDLK_X) && d->selectAll)
        {
            SDL_SetClipboardText(d->text.c_str());
            if (key == SDLK_X) { d->text.clear(); d->cursor = 0; d->selectAll = false; }
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
            d->selectAll = false; d->error.clear();
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
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        const float px = event->button.x, py = event->button.y;
        if (ui_inside(px, py, x + 24, y + 92, 712, 38)) { d->Focus(0); d->cursor = d->text.size(); d->selectAll = true; }
        else if (ui_inside(px, py, x + 508, y + 366, 92, 30) || ui_inside(px, py, x + 704, y + 18, 32, 28)) d->Close();
        else if (ui_inside(px, py, x + 612, y + 366, 124, 30)) d->Add();
        else for (int field = 1; field < 3; ++field)
        {
            const float top = y + 164 + (field - 1) * 64;
            if (!ui_inside(px, py, x + 132, top, 604, 32)) continue;
            d->Focus(field);
            int &value = field == 1 ? d->font : d->shape;
            const int count = field == 1 ? int(d->fonts.size()) : 10;
            if (px < x + 170) value = (value + count - 1) % count;
            else if (px >= x + 698) value = (value + 1) % count;
        }
    }
    return event->type != SDL_EVENT_QUIT && event->type != SDL_EVENT_WINDOW_CLOSE_REQUESTED;
}
void text_dialog_draw(MarcusTextDialog *d, MarcusUI *ui, int width, int height)
{
    if (!text_dialog_visible(d)) return;
    const float x = (width - 760) / 2.f, y = (height - 420) / 2.f;
    ui_fill(ui, 0, 0, float(width), float(height), 0x121519);
    ui_fill(ui, x - 1, y - 1, 762, 422, 0x5b6570);
    ui_fill(ui, x, y, 760, 420, 0x25282d);
    ui_heading(ui, x + 24, y + 22, "NEW 3D TEXT", 0x78cbc4);
    ui_button(ui, x + 704, y + 18, 32, "x", 0, 1);
    char count[96]; SDL_snprintf(count, sizeof(count), "Text / %d of 50 characters", int(SDL_utf8strlen(d->text.c_str())));
    ui_text(ui, x + 24, y + 66, count, 0xdde0e5);
    ui_fill(ui, x + 23, y + 91, 714, 40, !d->focus ? 0x78cbc4 : 0x424952);
    ui_fill(ui, x + 24, y + 92, 712, 38, !d->focus && d->selectAll ? 0x375e64 : 0x1c1e22);
    std::string display = d->text;
    if (!d->focus && !d->selectAll && SDL_GetTicks() % 1000 < 500) display.insert(d->cursor, "|");
    ui_label(ui, x + 34, y + 104, 692, display.c_str(), 0xdde0e5);
    for (int field = 1; field < 3; ++field)
    {
        const float top = y + 164 + (field - 1) * 64;
        ui_text(ui, x + 24, top + 8, field == 1 ? "3D font" : "Shape", 0xdde0e5);
        ui_button_sized(ui, x + 132, top, 38, 32, "<", d->focus == field, 1);
        ui_fill(ui, x + 178, top, 512, 32, 0x1c1e22);
        ui_label(ui, x + 190, top + 8, 488, field == 1 ? d->fonts[d->font].name : shapes[d->shape], 0xdde0e5);
        ui_button_sized(ui, x + 698, top, 38, 32, ">", d->focus == field, 1);
    }
    ui_text(ui, x + 24, y + 288, "Inserted at this frame using the camera's next insertion point.", 0x969da8);
    ui_label(ui, x + 24, y + 324, 712, d->error.c_str(), 0xe4a1a1);
    ui_text(ui, x + 24, y + 376, "Tab to switch / Arrows to choose / Enter to add", 0x969da8);
    ui_button_sized(ui, x + 508, y + 366, 92, 30, "Cancel", 0, 1);
    ui_button_sized(ui, x + 612, y + 366, 124, 30, "Add 3D text", 1, 1);
}
int text_dialog_take(MarcusTextDialog *d)
{
    if (!d) return -1;
    const int chosen = d->chosen; d->chosen = -1; return chosen;
}
}
