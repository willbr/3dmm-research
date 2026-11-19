#include "caption_editor.h"
#include "colour_picker.h"
#include "dialog_host.h"
#include "dialog_widgets.h"
#include "mm/caption_render.h"
#include "mm/rich_text_layout.h"
#include "mm/text_access.h"
#include "ui.h"

enum { FINISH_NONE, FINISH_APPLY, FINISH_CANCEL, FINISH_DELETE };
enum { KEEP_BOX = -2, CAPTION_REJECTED = -1 };
typedef struct CaptionFontTile
{
    SDL_Texture *texture;
    int tried;
} CaptionFontTile;
struct MarcusCaptionEditor
{
    MarcusCaptionList *list;
    MarcusCaptionOptions value;
    CaptionFontTile *font_tiles;
    int font_count;
    char font_filter[128];
    float ink_hsl[3], paper_hsl[3];
    char text[MARCUS_CAPTION_UTF8_CAPACITY], error[256], preview_error[256];
    int selected, focus_text, generation, drag_kind, finish;
    int drag_x, drag_y, drag_width, drag_height;
    int draft_dirty, text_active, caret, selection_start, selection_end;
    int mouse_offset, mouse_anchor, selecting, mouse_word;
    ImVec2 drag_origin;
};

MarcusCaptionEditor *caption_editor_create(void)
{
    return (MarcusCaptionEditor *)SDL_calloc(1, sizeof(MarcusCaptionEditor));
}
static void release_list(MarcusCaptionEditor *editor)
{
    int i;
    if (editor->font_tiles)
        for (i = 0; i < editor->font_count; ++i)
            dialog_retire_texture(editor->font_tiles[i].texture);
    SDL_free(editor->font_tiles);
    editor->font_tiles = NULL;
    editor->font_count = -1;
    marcus_captions_destroy(editor->list);
    editor->list = NULL;
}
void caption_editor_destroy(MarcusCaptionEditor *editor)
{
    if (editor) release_list(editor);
    SDL_free(editor);
}
static void update_draft(MarcusCaptionEditor *editor, MarcusPlayer *player)
{
    editor->draft_dirty = 0;
    if (marcus_caption_draft(player, editor->list, editor->selected, &editor->value, editor->text))
    {
        editor->preview_error[0] = 0;
        editor->error[0] = 0;
    }
    else
        SDL_strlcpy(editor->preview_error, marcus_error(player), sizeof(editor->preview_error));
}
static void select_caption(MarcusCaptionEditor *editor, MarcusPlayer *player, int index)
{
    const char *text;
    text = marcus_caption(editor->list, index, &editor->value);
    if (!text) return;
    SDL_strlcpy(editor->text, text, sizeof(editor->text));
    editor->selected = index;
    editor->error[0] = 0;
    editor->preview_error[0] = 0;
    editor->focus_text = 1;
    editor->text_active = editor->selecting = 0;
    editor->caret = editor->selection_start = editor->selection_end = 0;
    editor->drag_kind = 0;
    editor->finish = FINISH_NONE;
    ++editor->generation;
    marcus_caption_preview(player, editor->list, index);
    update_draft(editor, player);
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
static void choose_font(MarcusCaptionEditor *editor, MarcusPlayer *player, SDL_Renderer *renderer)
{
    const char *name;
    CaptionFontTile *tile;
    MarcusImage sample;
    float width;
    int i, shown, selected, choose;
    igTextUnformatted("Default font", NULL);
    igSetNextItemWidth(-1);
    igSetNextWindowSizeConstraints(dialog_vec(260, 0), dialog_vec(450, 370), NULL, NULL);
    if (!igBeginCombo("##Default font", editor->value.font, 0)) return;
    if (editor->font_count < 0)
    {
        editor->font_count = marcus_caption_font_count(player, editor->list);
        editor->font_tiles = (CaptionFontTile *)SDL_calloc((size_t)editor->font_count, sizeof(CaptionFontTile));
    }
    if (igIsWindowAppearing())
    {
        editor->font_filter[0] = 0;
        igSetKeyboardFocusHere(0);
    }
    igSetNextItemWidth(-1);
    igInputTextWithHint("##Filter fonts", "Search fonts", editor->font_filter, sizeof(editor->font_filter), 0, NULL, NULL);
    choose = -1;
    if (igBeginChild_Str("Font list", dialog_vec(0, 248), 0, 0))
    {
        width = igGetContentRegionAvail().x;
        shown = 0;
        for (i = 0; i < editor->font_count; ++i)
        {
            name = marcus_caption_font_name(editor->list, i);
            if (!SDL_strcasestr(name, editor->font_filter)) continue;
            ++shown;
            igPushID_Int(i);
            selected = SDL_strcasecmp(name, editor->value.font) == 0;
            if (igSelectable_Bool(name, selected, ImGuiSelectableFlags_NoAutoClosePopups, dialog_vec(width * .53f, 26)))
                choose = i;
            if (igIsItemHovered(0)) igSetItemTooltip("%s", name);
            if (igIsItemVisible() && editor->font_tiles)
            {
                tile = &editor->font_tiles[i];
                if (!tile->tried)
                {
                    tile->tried = 1;
                    if (marcus_caption_font_sample(player, editor->list, i, &sample))
                        tile->texture = dialog_preview_texture(renderer, sample.rgba, sample.width, sample.height, sample.pitch);
                }
                igSameLine(0, 8);
                dialog_image(tile->texture, dialog_vec(SDL_max(1, width * .47f - 8), 26));
            }
            igPopID();
        }
        if (!shown) igTextDisabled(editor->font_count ? "No matching fonts" : "No fonts available");
    }
    igEndChild();
    if (choose >= 0)
    {
        name = marcus_caption_font_name(editor->list, choose);
        SDL_strlcpy(editor->value.font, name, sizeof(editor->value.font));
        igCloseCurrentPopup();
    }
    igEndCombo();
}
static ImVec4 colour(unsigned long rgb, float alpha);
static void edit_colour(const char *label, unsigned long *value, float hsl[3])
{
    igPushID_Str(label);
    if (igColorButton("##Colour", colour(*value, 1), ImGuiColorEditFlags_NoTooltip, dialog_vec(24, 0)))
    {
        marcus_rgb_okhsl((unsigned)*value, hsl);
        igOpenPopup_Str("Colour", 0);
    }
    igSameLine(0, 4);
    igTextUnformatted(label, NULL);
    if (igBeginPopup("Colour", 0))
    {
        if (dialog_okhsl(hsl)) *value = marcus_okhsl_rgb(hsl);
        igText("#%06lX", *value);
        igEndPopup();
    }
    igPopID();
}
static void fields(MarcusCaptionEditor *editor, MarcusPlayer *player, SDL_Renderer *renderer)
{
    static const char *alignments[] = {"Left", "Center", "Right"};
    static const int align_values[] = {0, 2, 1};
    MarcusCaptionOptions *value;
    ImVec4 popup;
    int alignment, pair[2];
    value = &editor->value;
    popup = igGetStyle()->Colors[ImGuiCol_PopupBg];
    popup.w = 1;
    igPushStyleColor_Vec4(ImGuiCol_PopupBg, popup);
    choose_font(editor, player, renderer);
    igSetNextItemWidth(88);
    igInputInt("Font size", &value->font_size, 1, 0, 0);
    dialog_checkbox("Bold", &value->bold);
    igSameLine(0, 12); dialog_checkbox("Italic", &value->italic);

    alignment = value->alignment == 1 ? 2 : value->alignment == 2 ? 1 : 0;
    igSetNextItemWidth(105);
    if (igCombo_Str_arr("Alignment", &alignment, alignments, SDL_arraysize(alignments), -1))
        value->alignment = align_values[alignment];
    edit_colour("Text", &value->ink, editor->ink_hsl);
    igSameLine(0, 12); igBeginDisabled(value->transparent);
    edit_colour("Fill", &value->paper, editor->paper_hsl); igEndDisabled();
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
    igPopStyleColor(1);
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
static int caption_text_byte(void *text, MMI32 offset, MMU32 *ch)
{
    *ch = ((const unsigned char *)text)[offset];
    return 1;
}
static unsigned caption_word_flags(void *unused, MMU32 ch)
{
    (void)unused;
    return mm_rich_text_flags(ch);
}
static void caption_word(ImGuiInputTextCallbackData *data, int hit, MMI32 *first, MMI32 *end)
{
    MMTextAccessServices access;
    unsigned breaks;
    breaks = MM_TEXT_CHAR_IGNORE | MM_TEXT_CHAR_MAY_BREAK;
    if (hit >= data->BufTextLen || (mm_rich_text_flags((unsigned char)data->Buf[hit]) & breaks))
        return;
    /* The native word breaks are ASCII, so UTF-8 bytes within a letter stay together. */
    SDL_zero(access);
    access.context = data->Buf;
    access.character = caption_text_byte;
    access.classify = caption_word_flags;
    mm_text_navigate(MM_TEXT_NAV_PREVIOUS, hit + 1, 1, data->BufTextLen, first, &access);
    mm_text_navigate(MM_TEXT_NAV_NEXT, *first, 1, data->BufTextLen, end, &access);
    while (*end > *first && (mm_rich_text_flags((unsigned char)data->Buf[*end - 1]) & breaks))
        --*end;
}

/* ImGui owns the text buffer and editing. Supply selection offsets from the
 * movie's layout as they differ from the widget's font and wrapping. */
static int edit_cursor(ImGuiInputTextCallbackData *data)
{
    MarcusCaptionEditor *editor;
    MMI32 first, end;
    editor = (MarcusCaptionEditor *)data->UserData;
    if (editor->mouse_offset >= 0 && editor->mouse_offset <= data->BufTextLen &&
        editor->mouse_anchor <= data->BufTextLen)
    {
        first = editor->mouse_anchor;
        end = editor->mouse_offset;
        if (editor->mouse_word)
        {
            caption_word(data, editor->mouse_offset, &first, &end);
            editor->mouse_anchor = first;
        }
        data->CursorPos = end;
        data->SelectionStart = first;
        data->SelectionEnd = end;
    }
    editor->caret = data->CursorPos;
    editor->selection_start = data->SelectionStart;
    editor->selection_end = data->SelectionEnd;
    return 0;
}
static int closest_stop(const MarcusCaptionStop *stops, int count, float x, float y)
{
    float dy, dx, best_y, best_x;
    int i, best;
    best = 0;
    best_y = best_x = SDL_MAX_SINT32;
    for (i = 0; i < count; ++i)
    {
        dy = SDL_max(stops[i].y - y, y - (stops[i].y + stops[i].height));
        dy = SDL_max(0, dy);
        dx = SDL_fabsf(stops[i].x - x);
        if (dy < best_y || (dy == best_y &&
            (stops[i].y == stops[best].y ? dx < best_x : stops[i].y > stops[best].y)))
        {
            best = i;
            best_y = dy;
            best_x = dx;
        }
    }
    return best;
}
static void draw_selection(const MarcusCaptionEditor *editor, const SDL_FRect *canvas, const SDL_FRect *clip,
                           const MarcusCaptionStop *stops, int count, int scroll)
{
    ImDrawList *draw;
    ImU32 highlight;
    float sx, sy, x, y, right;
    int i, start, end, caret;
    draw = igGetWindowDrawList();
    highlight = igColorConvertFloat4ToU32(colour(0x4b8ee1, .38f));
    sx = canvas->w / MM_CAPTION_WIDTH;
    sy = canvas->h / MM_CAPTION_HEIGHT;
    start = SDL_min(editor->selection_start, editor->selection_end);
    end = SDL_max(editor->selection_start, editor->selection_end);
    caret = -1;
    ImDrawList_PushClipRect(draw, dialog_vec(clip->x, clip->y),
                           dialog_vec(clip->x + clip->w, clip->y + clip->h), 1);
    for (i = 0; i < count; ++i)
    {
        if (stops[i].offset == editor->caret) caret = i;
        if (i + 1 == count || stops[i].offset < start || stops[i].offset >= end || stops[i].y != stops[i + 1].y)
            continue;
        x = canvas->x + stops[i].x * sx;
        y = canvas->y + (stops[i].y - scroll) * sy;
        right = canvas->x + stops[i + 1].x * sx;
        ImDrawList_AddRectFilled(draw, dialog_vec(x, y),
            dialog_vec(SDL_max(x + 2, right), y + stops[i].height * sy), highlight, 0, 0);
    }
    if (caret >= 0)
    {
        x = canvas->x + stops[caret].x * sx;
        y = canvas->y + (stops[caret].y - scroll) * sy;
        ImDrawList_AddLine(draw, dialog_vec(x, y), dialog_vec(x, y + stops[caret].height * sy),
                           igColorConvertFloat4ToU32(colour(0, .85f)), 3);
        ImDrawList_AddLine(draw, dialog_vec(x, y), dialog_vec(x, y + stops[caret].height * sy),
                           igColorConvertFloat4ToU32(colour(0xffffff, 1)), 1);
    }
    ImDrawList_PopClipRect(draw);
}
static void draw_box(MarcusCaptionEditor *editor, MarcusPlayer *player, const SDL_FRect *canvas)
{
    static const int kinds[] = {DRAG_LEFT | DRAG_TOP, DRAG_TOP, DRAG_RIGHT | DRAG_TOP, DRAG_RIGHT,
        DRAG_RIGHT | DRAG_BOTTOM, DRAG_BOTTOM, DRAG_LEFT | DRAG_BOTTOM, DRAG_LEFT};
    static const int cursors[] = {ImGuiMouseCursor_ResizeNWSE, ImGuiMouseCursor_ResizeNS,
        ImGuiMouseCursor_ResizeNESW, ImGuiMouseCursor_ResizeEW, ImGuiMouseCursor_ResizeNWSE,
        ImGuiMouseCursor_ResizeNS, ImGuiMouseCursor_ResizeNESW, ImGuiMouseCursor_ResizeEW};
    static const float anchors[8][2] = {{0, 0}, {.5f, 0}, {1, 0}, {1, .5f}, {1, 1}, {.5f, 1}, {0, 1}, {0, .5f}};
    SDL_FRect box, hit, text;
    const MarcusCaptionStop *stops;
    ImVec2 mouse;
    ImGuiIO *io;
    ImDrawList *draw;
    ImU32 border;
    float grip, sx, sy, font, x, y;
    int i, count, scroll, click, shift, styles, vars;
    editor->text_active = 0;
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
    text.x = box.x + SDL_min(box.w / 4, MM_CAPTION_BORDER * sx);
    text.y = box.y + SDL_min(box.h / 4, MM_CAPTION_BORDER * sy);
    text.w = box.w - 2 * (text.x - box.x); text.h = box.h - 2 * (text.y - box.y);
    stops = marcus_caption_positions(player, &count, &scroll);
    editor->mouse_offset = -1;
    editor->mouse_word = 0;
    click = count && !editor->drag_kind && igIsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        igIsMouseHoveringRect(dialog_vec(text.x, text.y), dialog_vec(text.x + text.w, text.y + text.h), 1)
        ? igGetMouseClickedCount(ImGuiMouseButton_Left) : 0;
    io = igGetIO_Nil();
    shift = io->KeyShift;
    if (!igIsMouseDown_Nil(ImGuiMouseButton_Left)) editor->selecting = 0;
    if (click && (shift || click == 1 || !(click & 1)))
    {
        mouse = igGetMousePos();
        x = (mouse.x - canvas->x) / sx;
        y = (mouse.y - canvas->y) / sy + scroll;
        i = closest_stop(stops, count, x, y);
        editor->mouse_offset = stops[i].offset;
        if (click > 1 && !shift)
        {
            /* A word hit uses the glyph's span rather than its nearest caret.
             * On the right half of a glyph, the nearest caret follows it. */
            if (i > 0 && x < stops[i].x && stops[i - 1].y == stops[i].y) --i;
            if (i + 1 < count && stops[i].y == stops[i + 1].y &&
                x >= stops[i].x && x < stops[i + 1].x && y >= stops[i].y && y < stops[i].y + stops[i].height)
            {
                editor->mouse_offset = stops[i].offset;
                editor->mouse_word = 1;
            }
        }
        editor->mouse_anchor = shift ? editor->selection_start : editor->mouse_offset;
        editor->selecting = 1;
    }
    else if (count && editor->selecting && (io->MouseDelta.x || io->MouseDelta.y))
    {
        editor->mouse_offset = editor->caret;
        if (igIsMouseDragging(ImGuiMouseButton_Left, -1))
        {
            mouse = igGetMousePos();
            i = closest_stop(stops, count, (mouse.x - canvas->x) / sx, (mouse.y - canvas->y) / sy + scroll);
            editor->mouse_offset = stops[i].offset;
        }
    }
    font = SDL_clamp(editor->value.font_size * sy, 8.0f, 72.0f);
    igPushFont(NULL, font);
    igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding,
                        dialog_vec(SDL_min(text.w / 4, (MM_CAPTION_INDENT - MM_CAPTION_BORDER) * sx), 0));
    vars = 1;
    if (count)
    {
        igPushStyleVar_Float(ImGuiStyleVar_ScrollbarSize, 0);
        ++vars;
        igPushStyleColor_U32(ImGuiCol_FrameBg, 0);
        igPushStyleColor_U32(ImGuiCol_Text, 0);
        igPushStyleColor_U32(ImGuiCol_InputTextCursor, 0);
        igPushStyleColor_U32(ImGuiCol_TextSelectedBg, 0);
        styles = 4;
    }
    else
    {
        igPushStyleColor_Vec4(ImGuiCol_FrameBg, colour(editor->value.paper, editor->value.transparent ? 0 : 1));
        igPushStyleColor_Vec4(ImGuiCol_Text, colour(editor->value.ink, 1));
        styles = 2;
    }
    igSetCursorScreenPos(dialog_vec(text.x, text.y));
    if (editor->focus_text)
    {
        igSetKeyboardFocusHere(0);
        editor->focus_text = 0;
    }
    if (igInputTextMultiline("##words", editor->text, sizeof(editor->text), dialog_vec(text.w, text.h),
                             ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CallbackAlways, edit_cursor, editor))
        editor->draft_dirty = 1;
    editor->text_active = igIsItemActive();
    if (count && editor->text_active)
        draw_selection(editor, canvas, &text, stops, count, scroll);
    igPopStyleColor(styles);
    igPopStyleVar(vars);
    igPopFont();
    igPopID();
}
static int caption_canvas(const SDL_FRect *view, const MarcusImage *image, SDL_FRect *canvas)
{
    MMPixelRect clip;
    float aspect;
    aspect = image->scene_camera.valid ? image->scene_camera.aspect : (float)MM_CAPTION_WIDTH / MM_CAPTION_HEIGHT;
    if (view->w <= 0 || view->h <= 0 || !mm_caption_viewport(&clip, image->width, image->height, aspect)) return 0;
    canvas->x = view->x + clip.left * view->w / image->width;
    canvas->y = view->y + clip.top * view->h / image->height;
    canvas->w = (clip.right - clip.left) * view->w / image->width;
    canvas->h = (clip.bottom - clip.top) * view->h / image->height;
    return 1;
}
int caption_editor_hit(const MarcusCaptionEditor *editor, const SDL_FRect *view, const MarcusImage *image,
                       float x, float y)
{
    SDL_FRect canvas, box;
    float margin;
    if (!caption_canvas(view, image, &canvas) || !ui_inside(x, y, canvas.x, canvas.y, canvas.w, canvas.h))
        return 0;
    box = box_rect(&canvas, &editor->value);
    if (box.w <= 0 || box.h <= 0 || !SDL_HasRectIntersectionFloat(&box, &canvas)) return 0;
    margin = SDL_min(8.0f, SDL_min(box.w, box.h) / 3) / 2;
    return ui_inside(x, y, box.x - margin, box.y - margin, box.w + 2 * margin, box.h + 2 * margin);
}
static void draw_canvas(MarcusCaptionEditor *editor, MarcusPlayer *player, const SDL_FRect *view,
                        const MarcusImage *image)
{
    SDL_FRect canvas;
    if (!caption_canvas(view, image, &canvas)) return;
    if (editor->focus_text) igSetNextWindowFocus();
    if (ui_panel("2D text canvas", canvas.x, canvas.y, canvas.w, canvas.h, 1, 0))
        draw_box(editor, player, &canvas);
    ui_panel_end();
}
static int draw_inspector(MarcusCaptionEditor *editor, MarcusPlayer *player, SDL_Renderer *renderer, const SDL_FRect *area)
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
            fields(editor, player, renderer);
        }
        igEndChild();
        if (editor->error[0]) igTextWrapped("%s", editor->error);
        else if (editor->preview_error[0]) igTextWrapped("%s", editor->preview_error);
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
int caption_editor_draw(MarcusCaptionEditor *editor, MarcusPlayer *player, SDL_Renderer *renderer, const SDL_FRect *view,
                        const MarcusImage *image, const SDL_FRect *inspector)
{
    MarcusCaptionOptions before;
    int selection, redraw;
    SDL_memcpy(&before, &editor->value, sizeof(before));
    selection = draw_inspector(editor, player, renderer, inspector);
    draw_canvas(editor, player, view, image);
    if (editor->finish) return finish_caption(editor, player);
    if (selection != KEEP_BOX && selection != editor->selected)
        return switch_caption(editor, player, selection);
    redraw = 0;
    if (editor->draft_dirty || SDL_memcmp(&before, &editor->value, sizeof(before)))
    {
        update_draft(editor, player);
        redraw = CAPTION_REDRAW;
    }
    if (marcus_caption_caret(player, editor->text_active ? editor->caret : -1))
        redraw = CAPTION_REDRAW;
    return redraw;
}
