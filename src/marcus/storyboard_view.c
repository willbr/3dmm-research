#include "storyboard_view.h"
#include "dialog_widgets.h"

static void command(MarcusStoryboardEditor *editor, int id, int index)
{ storyboard_editor_command(editor, id, index, 0, NULL); }
static void guide(MarcusUI *ui, const MarcusCameraGuide *shape, const SDL_FRect *rect, unsigned long colour)
{
    int i;
    const MarcusGuideLine *line;
    for (i = 0; i < shape->line_count; ++i) {
        line = shape->lines + i;
        ui_line(ui, rect->x + line->x1 * rect->w, rect->y + line->y1 * rect->h,
            rect->x + line->x2 * rect->w, rect->y + line->y2 * rect->h, colour);
    }
}
static ImVec4 rgb(unsigned long value)
{
    ImVec4 c;
    c.x = ((value >> 16) & 255) / 255.0f; c.y = ((value >> 8) & 255) / 255.0f;
    c.z = (value & 255) / 255.0f; c.w = 1; return c;
}
static void paint_controls(MarcusStoryboardEditor *editor, MarcusBoardEditorState *s)
{
    static const int swatches[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
    static const int sizes[] = {1, 4, 8, 16, 32};
    ImVec2 at;
    ImVec4 colour;
    int i, x, y;
    char label[48];
    for (i = 0; i < 10; ++i) {
        if (i % 5) igSameLine(0, 4);
        igPushID_Int(i);
        if (igColorButton("Paint swatch", rgb(s->swatches[swatches[i]]), ImGuiColorEditFlags_NoTooltip, dialog_vec(40, 24))) command(editor, BOARD_SWATCH, swatches[i]);
        igPopID();
    }
    if (dialog_okhsl(s->okhsl)) storyboard_editor_command(editor, BOARD_HSL, 0, 0, s->okhsl);
    igText("Paint / #%06lX", s->rgb);
    at = igGetCursorScreenPos(); igDummy(dialog_vec(224, 28));
    for (y = 0; y < 4; ++y) for (x = 0; x < 16; ++x) {
        colour = rgb(s->dither[y * 16 + x]);
        ImDrawList_AddRectFilled(igGetWindowDrawList(), dialog_vec(at.x + x * 14, at.y + y * 7),
            dialog_vec(at.x + (x + 1) * 14, at.y + (y + 1) * 7), igColorConvertFloat4ToU32(colour), 0, 0);
    }
    igSeparatorText("Brush size");
    for (i = 0; i < 5; ++i) {
        if (i) igSameLine(0, 4);
        SDL_snprintf(label, sizeof(label), "%d", sizes[i]);
        if (igRadioButton_Bool(label, s->brush == sizes[i])) command(editor, BOARD_BRUSH, sizes[i]);
    }
    if (dialog_checkbox("Pick colour (I)", &s->picking)) command(editor, BOARD_PICK, 0);
    igBeginDisabled(!s->can_bake);
    if (igButton("Bake grid", dialog_vec(-1, 28))) command(editor, BOARD_BAKE, 0);
    igEndDisabled();
    if (igButton("Clear angle", dialog_vec(112, 28))) command(editor, BOARD_CLEAR, 0);
    igSameLine(0, 8); igBeginDisabled(!s->can_undo);
    if (igButton("Undo", dialog_vec(104, 28))) command(editor, BOARD_UNDO, 0);
    igEndDisabled(); igTextWrapped("0-9 colours / [ ] size / Alt pick / Ctrl+Z undo");
}
void storyboard_editor_draw(MarcusStoryboardEditor *editor, MarcusUI *ui)
{
    MarcusBoardEditorState s;
    MarcusBoardField *field;
    MarcusBoardLightOverlay *light;
    SDL_Rect clip;
    unsigned long colour;
    int i, pass, changed;
    float value;
    storyboard_editor_state(editor, &s);
    if (!s.open) return;
    if (ui_panel("Storyboard camera toolbar", s.bar.x, s.bar.y, s.bar.w, 36, 1, 0)) {
        igBeginDisabled(s.angle <= 0); if (igButton("<", dialog_vec(28, 24))) command(editor, BOARD_ANGLE, s.angle - 1); igEndDisabled();
        igSameLine(0, 8); igBeginDisabled(s.angle + 1 >= s.angles); if (igButton(">", dialog_vec(28, 24))) command(editor, BOARD_ANGLE, s.angle + 1); igEndDisabled();
        igSameLine(0, 8); igText("Angle %d / %d", s.angle + 1, s.angles);
        igSameLine(0, 16); if (dialog_checkbox("Grid", &s.grid)) command(editor, BOARD_GRID, 0);
        igSameLine(0, 16); igBeginDisabled(s.angles >= 64); if (igButton("New angle", dialog_vec(100, 24))) command(editor, BOARD_NEW_ANGLE, 0); igEndDisabled();
        igSameLine(0, 8); igBeginDisabled(!s.has_work); if (igButton("Use work camera", dialog_vec(140, 24))) command(editor, BOARD_USE_WORK, 0); igEndDisabled();
    }
    ui_panel_end();
    if (ui_panel("Storyboard tools", s.tools.x, s.tools.y, s.tools.w, s.tools.h, 0, 1)) {
        igTextUnformatted("Storyboard tools", NULL); igSameLine(0, 24);
        if (igButton("Done", dialog_vec(64, 24))) command(editor, BOARD_DONE, 0);
        for (i = 0; i < 3; ++i) {
            if (i) igSameLine(0, 10);
            if (igRadioButton_Bool(i == 0 ? "Paint" : i == 1 ? "Camera" : "Lights", s.tab == i)) command(editor, BOARD_TAB, i);
        }
        igSeparator();
        if (s.tab == 0) paint_controls(editor, &s);
        else {
            if (s.tab == 2) {
                igText("Light %d / %d", s.lights ? s.light + 1 : 0, s.lights);
                igBeginDisabled(s.light <= 0); if (igButton("Previous", dialog_vec(100, 24))) command(editor, BOARD_LIGHT, s.light - 1); igEndDisabled();
                igSameLine(0, 8); igBeginDisabled(s.light + 1 >= s.lights); if (igButton("Next", dialog_vec(100, 24))) command(editor, BOARD_LIGHT, s.light + 1); igEndDisabled();
                igBeginDisabled(s.lights >= 16); if (igButton("Add light", dialog_vec(100, 24))) command(editor, BOARD_LIGHT_ADD, 0); igEndDisabled();
                igSameLine(0, 8); igBeginDisabled(!s.lights); if (igButton("Delete light", dialog_vec(100, 24))) command(editor, BOARD_LIGHT_REMOVE, 0); igEndDisabled();
                igBeginDisabled(!s.lights); if (dialog_checkbox("Point light", &s.point)) command(editor, BOARD_POINT, 0); igEndDisabled();
            }
            for (i = 0; i < s.field_count; ++i) {
                field = s.fields + i; value = field->value; igSetNextItemWidth(150);
                changed = igDragFloat(field->name, &value, field->step * .2f, field->low, field->high, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                if (igIsItemActivated()) command(editor, BOARD_FIELD_BEGIN, i);
                if (changed) storyboard_editor_command(editor, BOARD_FIELD_SET, i, value, NULL);
                if (s.editing == i && (igIsKeyPressed_Bool(ImGuiKey_Escape, 0) ||
                    (!igGetIO_Nil()->WantTextInput && (igGetIO_Nil()->KeyCtrl || igGetIO_Nil()->KeySuper) && igIsKeyPressed_Bool(ImGuiKey_Z, 0)))) command(editor, BOARD_FIELD_CANCEL, i);
                else if (igIsItemDeactivated()) command(editor, BOARD_FIELD_END, i);
            }
            if (s.tab == 2) {
                if (igButton("Frame lights", dialog_vec(120, 28))) command(editor, BOARD_FRAME_LIGHTS, 0);
                igSameLine(0, 8); if (igButton(s.inspecting ? "Shot view" : "3D light view", dialog_vec(120, 28))) command(editor, BOARD_SHOT_VIEW, 0);
            }
            igTextWrapped("Hold RMB / WASD + Q E / Shift to move faster / Wheel changes speed.");
        }
        igSeparator(); igTextWrapped("Edits join the movie as you work. Save copy / Ctrl+S to keep them.");
        if (s.error[0]) igTextWrapped("%s", s.error);
    }
    ui_panel_end();
    storyboard_editor_prepare(editor, ui->renderer); storyboard_editor_state(editor, &s);
    if (!s.open) return;
    ui_image(ui, s.texture, &s.canvas);
    clip.x = (int)s.canvas.x; clip.y = (int)s.canvas.y; clip.w = (int)s.canvas.w; clip.h = (int)s.canvas.h;
    ui_clip(&clip);
    if (s.grid) ui_triangles(ui, s.vertices, s.indices, s.index_count);
    if (s.tab == 2) {
        guide(ui, &s.camera_guide, &s.canvas, 0x78b7c4);
        for (pass = 0; pass < 2; ++pass) for (i = 0; i < s.lights; ++i) {
            light = s.overlays + i;
            if (light->selected != pass) continue;
            colour = light->selected ? 0xffd77a : 0xc4a76d;
            guide(ui, &light->shape, &s.canvas, colour); guide(ui, &light->direction, &s.canvas, colour);
            if (light->visible) {
                ui_fill(ui, light->x - 3, light->y - 3, 6, 6, colour);
                ui_label(ui, light->x + 10, light->y - 20, ui_text_width(light->label), light->label, colour);
            }
        }
    }
    ui_unclip();
}
