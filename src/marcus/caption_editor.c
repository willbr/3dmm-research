#include "caption_editor.h"
#include "dialog_widgets.h"
#include "mm/caption_render.h"
#include "ui.h"

enum { FINISH_NONE, FINISH_APPLY, FINISH_CANCEL, FINISH_DELETE };
enum { KEEP_BOX = -2, CAPTION_REJECTED = -1 };
struct MarcusCaptionEditor
{
    MarcusCaptionList *list;
    MarcusCaptionOptions value;
    char text[MARCUS_CAPTION_UTF8_CAPACITY], error[256];
    int selected, focus_text, generation, drag_kind, finish;
    int drag_x, drag_y, drag_width, drag_height;
    ImVec2 drag_origin;
};

MarcusCaptionEditor *caption_editor_create(void)
{
    return (MarcusCaptionEditor *)SDL_calloc(1, sizeof(MarcusCaptionEditor));
}
static void release_list(MarcusCaptionEditor *editor)
{
    marcus_captions_destroy(editor->list);
    editor->list = NULL;
}
void caption_editor_destroy(MarcusCaptionEditor *editor)
{
    if (editor) release_list(editor);
    SDL_free(editor);
}
static void select_caption(MarcusCaptionEditor *editor, MarcusPlayer *player, int index)
{
    const char *text;
    text = marcus_caption(editor->list, index, &editor->value);
    if (!text) return;
    SDL_strlcpy(editor->text, text, sizeof(editor->text));
    editor->selected = index;
    editor->error[0] = 0;
    editor->focus_text = 1;
    editor->drag_kind = 0;
    editor->finish = FINISH_NONE;
    ++editor->generation;
    marcus_caption_preview(player, editor->list, index);
}
static int visible_at(const MarcusCaptionOptions *box, int frame)
{
    return mm_text_box_visible_range(box->first_frame,
        box->through_end || box->last_frame == SDL_MAX_SINT32 ? SDL_MAX_SINT32 : box->last_frame + 1, frame);
}
int caption_editor_show(MarcusCaptionEditor *editor, MarcusPlayer *player, const MarcusInfo *info, int add)
{
    MarcusCaptionOptions options;
    int index, selected, frame;
    release_list(editor);
    editor->list = marcus_captions(player);
    if (!editor->list) return 0;
    selected = -1;
    if (!add && marcus_caption_count(editor->list))
    {
        selected = 0;
        frame = (int)(info->frame - info->first_frame + 1);
        for (index = marcus_caption_count(editor->list) - 1; index >= 0; --index)
        {
            if (marcus_caption(editor->list, index, &options) && visible_at(&options, frame))
            {
                selected = index;
                break;
            }
        }
    }
    select_caption(editor, player, selected);
    return 1;
}
int caption_editor_visible(const MarcusCaptionEditor *editor) { return editor && editor->list; }

static int save_caption(MarcusCaptionEditor *editor, MarcusPlayer *player, int remove)
{
    unsigned long document;
    if (editor->selected < 0 && !editor->text[0]) return 0;
    document = marcus_document(player);
    if (!marcus_edit_caption(player, editor->list, editor->selected, &editor->value, remove ? NULL : editor->text))
    {
        SDL_strlcpy(editor->error, marcus_error(player), sizeof(editor->error));
        return CAPTION_REJECTED;
    }
    return marcus_document(player) != document ? CAPTION_COMMITTED : 0;
}
void caption_editor_request_finish(MarcusCaptionEditor *editor, int apply)
{
    editor->finish = apply ? FINISH_APPLY : FINISH_CANCEL;
}
static int finish_caption(MarcusCaptionEditor *editor, MarcusPlayer *player)
{
    int changed;
    changed = editor->finish == FINISH_CANCEL ? 0 : save_caption(editor, player, editor->finish == FINISH_DELETE);
    editor->finish = FINISH_NONE;
    if (changed == CAPTION_REJECTED) return 0;
    marcus_caption_preview(player, NULL, -1);
    release_list(editor);
    return changed | CAPTION_REDRAW;
}
static int switch_caption(MarcusCaptionEditor *editor, MarcusPlayer *player, int index)
{
    int changed;
    if (index == editor->selected) return 0;
    changed = save_caption(editor, player, 0);
    if (changed == CAPTION_REJECTED) return 0;
    if (changed)
    {
        release_list(editor);
        editor->list = marcus_captions(player);
        if (!editor->list)
        {
            marcus_caption_preview(player, NULL, -1);
            return changed | CAPTION_REDRAW;
        }
    }
    select_caption(editor, player, index);
    return changed | CAPTION_REDRAW;
}

static void box_label(const MarcusCaptionList *list, int index, char *label, size_t capacity)
{
    char words[72];
    char *line;
    const char *text;
    if (index < 0)
    {
        SDL_strlcpy(label, "New text box", capacity);
        return;
    }
    text = marcus_caption(list, index, NULL);
    SDL_utf8strlcpy(words, text, sizeof(words));
    for (line = words; *line; ++line)
    {
        if (*line == '\n' || *line == '\r') { *line = 0; break; }
        if (*line == '#') *line = ' ';
    }
    if (words[0])
        SDL_snprintf(label, capacity, "Box %d: %s", index + 1, words);
    else
        SDL_snprintf(label, capacity, "Box %d", index + 1);
}
static void edit_colour(const char *label, unsigned long *value)
{
    float rgb[3];
    rgb[0] = (float)((*value >> 16) & 255) / 255;
    rgb[1] = (float)((*value >> 8) & 255) / 255;
    rgb[2] = (float)(*value & 255) / 255;
    if (igColorEdit3(label, rgb, ImGuiColorEditFlags_NoInputs))
        *value = ((unsigned long)SDL_roundf(rgb[0] * 255) << 16) |
                 ((unsigned long)SDL_roundf(rgb[1] * 255) << 8) |
                  (unsigned long)SDL_roundf(rgb[2] * 255);
}
static void fields(MarcusCaptionOptions *value)
{
    static const char *alignments[] = {"Left", "Center", "Right"};
    static const int align_values[] = {0, 2, 1};
    int alignment, pair[2];
    igSetNextItemWidth(148);
    igInputText("Default font", value->font, sizeof(value->font), 0, NULL, NULL);
    igSetNextItemWidth(88);
    igInputInt("Font size", &value->font_size, 1, 0, 0);
    dialog_checkbox("Bold", &value->bold);
    igSameLine(0, 12); dialog_checkbox("Italic", &value->italic);

    alignment = value->alignment == 1 ? 2 : value->alignment == 2 ? 1 : 0;
    igSetNextItemWidth(105);
    if (igCombo_Str_arr("Alignment", &alignment, alignments, SDL_arraysize(alignments), -1))
        value->alignment = align_values[alignment];
    edit_colour("Text", &value->ink);
    igSameLine(0, 12); igBeginDisabled(value->transparent);
    edit_colour("Fill", &value->paper); igEndDisabled();
    igSameLine(0, 12); dialog_checkbox("Transparent", &value->transparent);

    pair[0] = value->x; pair[1] = value->y;
    igSetNextItemWidth(132);
    if (igInputInt2("Position (X, Y)", pair, 0)) { value->x = pair[0]; value->y = pair[1]; }
    pair[0] = value->width; pair[1] = value->height;
    igSetNextItemWidth(132);
    if (igInputInt2("Size (W, H)", pair, 0)) { value->width = pair[0]; value->height = pair[1]; }

    igSetNextItemWidth(105);
    igInputInt("First frame", &value->first_frame, 1, 0, 0);
    igBeginDisabled(value->through_end);
    igSetNextItemWidth(105);
    igInputInt("Last frame", &value->last_frame, 1, 0, 0);
    igEndDisabled();
    dialog_checkbox("Through end of scene", &value->through_end);
    dialog_checkbox("Scroll during playback", &value->scrolling);
}

static SDL_FRect box_rect(const SDL_FRect *canvas, const MarcusCaptionOptions *box)
{
    SDL_FRect rect;
    rect.x = canvas->x + box->x * canvas->w / MM_CAPTION_WIDTH;
    rect.y = canvas->y + box->y * canvas->h / MM_CAPTION_HEIGHT;
    rect.w = box->width * canvas->w / MM_CAPTION_WIDTH;
    rect.h = box->height * canvas->h / MM_CAPTION_HEIGHT;
    return rect;
}
enum { DRAG_LEFT = 1, DRAG_TOP = 2, DRAG_RIGHT = 4, DRAG_BOTTOM = 8, DRAG_MOVE = 16 };

static void drag_handle(MarcusCaptionEditor *editor, const SDL_FRect *canvas, const SDL_FRect *hit, int kind,
                        ImGuiMouseCursor cursor)
{
    ImVec2 mouse;
    MarcusCaptionOptions *box;
    int dx, dy, left, top, right, bottom, min_width, min_height;
    box = &editor->value;
    igPushID_Int(kind);
    igSetCursorScreenPos(dialog_vec(hit->x, hit->y));
    igInvisibleButton("##drag", dialog_vec(hit->w, hit->h), 0);
    if (igIsItemHovered(0) || igIsItemActive()) igSetMouseCursor(cursor);
    if (igIsItemActivated())
    {
        editor->drag_kind = kind;
        editor->drag_origin = igGetMousePos();
        editor->drag_x = box->x; editor->drag_y = box->y;
        editor->drag_width = box->width; editor->drag_height = box->height;
    }
    if (editor->drag_kind == kind && (igIsItemActive() || igIsItemDeactivated()))
    {
        mouse = igGetMousePos();
        dx = (int)SDL_roundf(SDL_clamp((mouse.x - editor->drag_origin.x) * MM_CAPTION_WIDTH / canvas->w,
                                      (float)-MM_CAPTION_WIDTH, (float)MM_CAPTION_WIDTH));
        dy = (int)SDL_roundf(SDL_clamp((mouse.y - editor->drag_origin.y) * MM_CAPTION_HEIGHT / canvas->h,
                                      (float)-MM_CAPTION_HEIGHT, (float)MM_CAPTION_HEIGHT));
        left = SDL_clamp(editor->drag_x, -4096, 4096);
        top = SDL_clamp(editor->drag_y, -4096, 4096);
        right = left + SDL_clamp(editor->drag_width, 1, 4096);
        bottom = top + SDL_clamp(editor->drag_height, 1, 4096);
        min_width = SDL_min(48, right - left);
        min_height = SDL_min(22, bottom - top);
        if (kind == DRAG_MOVE && (dx || dy))
        {
            box->x = SDL_clamp(left + dx, 0, SDL_max(0, MM_CAPTION_WIDTH - (right - left)));
            box->y = SDL_clamp(top + dy, 0, SDL_max(0, MM_CAPTION_HEIGHT - (bottom - top)));
        }
        else if (kind != DRAG_MOVE && (dx || dy))
        {
            if (kind & DRAG_LEFT) left = SDL_clamp(left + dx, SDL_min(0, right - min_width), right - min_width);
            if (kind & DRAG_RIGHT) right = SDL_clamp(right + dx, left + min_width,
                                                     SDL_max(left + min_width, MM_CAPTION_WIDTH));
            if (kind & DRAG_TOP) top = SDL_clamp(top + dy, SDL_min(0, bottom - min_height), bottom - min_height);
            if (kind & DRAG_BOTTOM) bottom = SDL_clamp(bottom + dy, top + min_height,
                                                       SDL_max(top + min_height, MM_CAPTION_HEIGHT));
            box->x = left; box->y = top;
            box->width = right - left; box->height = bottom - top;
        }
        else if (editor->value.x != editor->drag_x || editor->value.y != editor->drag_y ||
                 editor->value.width != editor->drag_width || editor->value.height != editor->drag_height)
        {
            box->x = editor->drag_x; box->y = editor->drag_y;
            box->width = editor->drag_width; box->height = editor->drag_height;
        }
        if (igIsItemDeactivated()) editor->drag_kind = 0;
    }
    igPopID();
}
static ImVec4 colour(unsigned long rgb, float alpha)
{
    ImVec4 result;
    result.x = (float)((rgb >> 16) & 255) / 255;
    result.y = (float)((rgb >> 8) & 255) / 255;
    result.z = (float)(rgb & 255) / 255;
    result.w = alpha;
    return result;
}
static void draw_box(MarcusCaptionEditor *editor, const SDL_FRect *canvas)
{
    static const int kinds[] = {DRAG_LEFT | DRAG_TOP, DRAG_TOP, DRAG_RIGHT | DRAG_TOP, DRAG_RIGHT,
        DRAG_RIGHT | DRAG_BOTTOM, DRAG_BOTTOM, DRAG_LEFT | DRAG_BOTTOM, DRAG_LEFT};
    static const int cursors[] = {ImGuiMouseCursor_ResizeNWSE, ImGuiMouseCursor_ResizeNS,
        ImGuiMouseCursor_ResizeNESW, ImGuiMouseCursor_ResizeEW, ImGuiMouseCursor_ResizeNWSE,
        ImGuiMouseCursor_ResizeNS, ImGuiMouseCursor_ResizeNESW, ImGuiMouseCursor_ResizeEW};
    static const float anchors[8][2] = {{0, 0}, {.5f, 0}, {1, 0}, {1, .5f}, {1, 1}, {.5f, 1}, {0, 1}, {0, .5f}};
    SDL_FRect box, hit, text;
    ImDrawList *draw;
    ImU32 border;
    float grip, sx, sy, font;
    int i;
    box = box_rect(canvas, &editor->value);
    if (box.w <= 0 || box.h <= 0 || !SDL_HasRectIntersectionFloat(&box, canvas)) return;
    sx = canvas->w / MM_CAPTION_WIDTH; sy = canvas->h / MM_CAPTION_HEIGHT;
    grip = SDL_min(8.0f, SDL_min(box.w, box.h) / 3);
    draw = igGetWindowDrawList();
    border = igColorConvertFloat4ToU32(colour(0x78cbc4, 1));
    ImDrawList_AddRect(draw, dialog_vec(box.x, box.y), dialog_vec(box.x + box.w, box.y + box.h), border, 0, 0, 1);
    igPushID_Int(editor->generation);
    for (i = 0; i < 8; ++i)
    {
        hit.x = box.x + box.w * anchors[i][0] - grip / 2;
        hit.y = box.y + box.h * anchors[i][1] - grip / 2;
        hit.w = hit.h = grip;
        drag_handle(editor, canvas, &hit, kinds[i], cursors[i]);
        ImDrawList_AddRectFilled(draw, dialog_vec(hit.x, hit.y), dialog_vec(hit.x + grip, hit.y + grip), border, 0, 0);
    }
    hit.x = box.x + grip / 2; hit.y = box.y - grip / 2;
    hit.w = box.w - grip; hit.h = grip;
    drag_handle(editor, canvas, &hit, DRAG_MOVE, ImGuiMouseCursor_ResizeAll);
    if (igIsItemHovered(0)) igSetItemTooltip("Drag the top border to move");
    text.x = box.x + SDL_min(box.w / 4, 5 * sx); text.y = box.y + SDL_min(box.h / 4, 5 * sy);
    text.w = box.w - 2 * (text.x - box.x); text.h = box.h - 2 * (text.y - box.y);
    font = SDL_clamp(editor->value.font_size * sy, 8.0f, 72.0f);
    igPushFont(NULL, font);
    igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, dialog_vec(SDL_min(text.w / 4, 9 * sx), 1));
    igPushStyleColor_Vec4(ImGuiCol_FrameBg, colour(editor->value.paper, editor->value.transparent ? 0 : 1));
    igPushStyleColor_Vec4(ImGuiCol_Text, colour(editor->value.ink, 1));
    igSetCursorScreenPos(dialog_vec(text.x, text.y));
    if (editor->focus_text)
    {
        igSetKeyboardFocusHere(0);
        editor->focus_text = 0;
    }
    igInputTextMultiline("##words", editor->text, sizeof(editor->text), dialog_vec(text.w, text.h),
                         ImGuiInputTextFlags_WordWrap, NULL, NULL);
    igPopStyleColor(2);
    igPopStyleVar(1);
    igPopFont();
    igPopID();
}
static int draw_canvas(MarcusCaptionEditor *editor, MarcusPlayer *player, const SDL_FRect *view,
                       const MarcusImage *image)
{
    MMPixelRect clip;
    SDL_FRect canvas, box;
    MarcusCaptionOptions option;
    MarcusInfo info;
    ImVec2 mouse;
    ImU32 outline;
    int frame, index, selected;
    float aspect;
    aspect = image->scene_camera.valid ? image->scene_camera.aspect : (float)MM_CAPTION_WIDTH / MM_CAPTION_HEIGHT;
    if (view->w <= 0 || view->h <= 0 || !mm_caption_viewport(&clip, image->width, image->height, aspect) ||
        !marcus_info(player, &info)) return KEEP_BOX;
    canvas.x = view->x + clip.left * view->w / image->width;
    canvas.y = view->y + clip.top * view->h / image->height;
    canvas.w = (clip.right - clip.left) * view->w / image->width;
    canvas.h = (clip.bottom - clip.top) * view->h / image->height;
    frame = (int)(info.frame - info.first_frame + 1);
    selected = -1;
    if (editor->focus_text) igSetNextWindowFocus();
    if (ui_panel("2D text canvas", canvas.x, canvas.y, canvas.w, canvas.h, 1, 0))
    {
        outline = igColorConvertFloat4ToU32(colour(0x78cbc4, .55f));
        for (index = 0; index < marcus_caption_count(editor->list); ++index)
            if (index != editor->selected && marcus_caption(editor->list, index, &option) && visible_at(&option, frame))
            {
                box = box_rect(&canvas, &option);
                ImDrawList_AddRect(igGetWindowDrawList(), dialog_vec(box.x, box.y),
                                   dialog_vec(box.x + box.w, box.y + box.h), outline, 0, 0, 1);
            }
        draw_box(editor, &canvas);
        if (igIsWindowHovered(0) && !igIsAnyItemHovered() && !editor->drag_kind &&
            igIsMouseClicked_Bool(ImGuiMouseButton_Left, 0))
        {
            mouse = igGetMousePos();
            selected = marcus_caption_at(player, editor->list,
                (int)((mouse.x - canvas.x) * MM_CAPTION_WIDTH / canvas.w),
                (int)((mouse.y - canvas.y) * MM_CAPTION_HEIGHT / canvas.h));
        }
    }
    ui_panel_end();
    return selected >= 0 ? selected : KEEP_BOX;
}
static int draw_inspector(MarcusCaptionEditor *editor, const SDL_FRect *area)
{
    char label[96];
    int index, select;
    select = KEEP_BOX;
    if (ui_panel("2D text properties", area->x, area->y, area->w, area->h, 0, 0))
    {
        igTextUnformatted("2D text", NULL);
        igSeparator();
        if (igBeginChild_Str("Text box options", dialog_vec(0, -64), 0, 0))
        {
            box_label(editor->list, editor->selected, label, sizeof(label));
            igSetNextItemWidth(-1);
            if (igBeginCombo("##Text box", label, 0))
            {
                for (index = -1; index < marcus_caption_count(editor->list); ++index)
                {
                    igPushID_Int(index);
                    box_label(editor->list, index, label, sizeof(label));
                    if (igSelectable_Bool(label, index == editor->selected, 0, dialog_vec(0, 0))) select = index;
                    if (index == editor->selected) igSetItemDefaultFocus();
                    igPopID();
                }
                igEndCombo();
            }
            fields(&editor->value);
        }
        igEndChild();
        if (editor->error[0]) igTextWrapped("%s", editor->error);
        else igTextDisabled("Type in the box on the canvas.");
        if (igButton("Done", dialog_vec(66, 0))) editor->finish = FINISH_APPLY;
        igSameLine(0, 10);
        if (igButton("Cancel", dialog_vec(66, 0))) editor->finish = FINISH_CANCEL;
        if (editor->selected >= 0)
        {
            igSameLine(0, 10);
            if (igButton("Delete box", dialog_vec(92, 0))) editor->finish = FINISH_DELETE;
        }
    }
    ui_panel_end();
    return select;
}
int caption_editor_draw(MarcusCaptionEditor *editor, MarcusPlayer *player, const SDL_FRect *view,
                        const MarcusImage *image, const SDL_FRect *inspector)
{
    int selection, clicked;
    selection = draw_inspector(editor, inspector);
    clicked = draw_canvas(editor, player, view, image);
    if (editor->finish) return finish_caption(editor, player);
    if (clicked != KEEP_BOX) selection = clicked;
    return selection != KEEP_BOX ? switch_caption(editor, player, selection) : 0;
}
