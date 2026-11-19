#include "caption_dialog.h"
#include "dialog_widgets.h"

struct MarcusCaptionDialog
{
    MarcusCaptionList *list;
    MarcusCaptionOptions value;
    char text[MARCUS_CAPTION_UTF8_CAPACITY], error[256];
    int open, selected, focus_text;
};

MarcusCaptionDialog *caption_dialog_create(void)
{
    return (MarcusCaptionDialog *)SDL_calloc(1, sizeof(MarcusCaptionDialog));
}
static void close_dialog(MarcusCaptionDialog *dialog)
{
    marcus_captions_destroy(dialog->list);
    dialog->list = NULL;
    dialog->open = 0;
}
void caption_dialog_destroy(MarcusCaptionDialog *dialog)
{
    if (dialog) close_dialog(dialog);
    SDL_free(dialog);
}
static void select_caption(MarcusCaptionDialog *dialog, int index)
{
    const char *text;
    text = marcus_caption(dialog->list, index, &dialog->value);
    if (!text) return;
    SDL_strlcpy(dialog->text, text, sizeof(dialog->text));
    dialog->selected = index;
    dialog->error[0] = 0;
    dialog->focus_text = 1;
}
int caption_dialog_show(MarcusCaptionDialog *dialog, MarcusPlayer *player, const MarcusInfo *info, int add)
{
    MarcusCaptionOptions options;
    int index, selected, frame;
    close_dialog(dialog);
    dialog->list = marcus_captions(player);
    if (!dialog->list) return 0;
    selected = -1;
    if (!add && marcus_caption_count(dialog->list))
    {
        selected = 0;
        frame = (int)(info->frame - info->first_frame + 1);
        for (index = marcus_caption_count(dialog->list) - 1; index >= 0; --index)
        {
            if (marcus_caption(dialog->list, index, &options) && frame >= options.first_frame &&
                (options.through_end || frame <= options.last_frame))
            {
                selected = index;
                break;
            }
        }
    }
    select_caption(dialog, selected);
    dialog->open = 1;
    return 1;
}
int caption_dialog_visible(const MarcusCaptionDialog *dialog) { return dialog && dialog->open; }

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
    igSetNextItemWidth(220);
    igInputText("Default font family", value->font, sizeof(value->font), 0, NULL, NULL);
    igSetNextItemWidth(105);
    igInputInt("Font size", &value->font_size, 1, 0, 0);
    igSameLine(0, 18); dialog_checkbox("Bold", &value->bold);
    igSameLine(0, 12); dialog_checkbox("Italic", &value->italic);

    alignment = value->alignment == 1 ? 2 : value->alignment == 2 ? 1 : 0;
    igSetNextItemWidth(105);
    if (igCombo_Str_arr("Alignment", &alignment, alignments, SDL_arraysize(alignments), -1))
        value->alignment = align_values[alignment];
    igSameLine(0, 18); edit_colour("Text", &value->ink);
    igSameLine(0, 12); dialog_checkbox("Transparent", &value->transparent);
    igSameLine(0, 12); igBeginDisabled(value->transparent);
    edit_colour("Fill", &value->paper); igEndDisabled();

    pair[0] = value->x; pair[1] = value->y;
    igSetNextItemWidth(132);
    if (igInputInt2("Position (X, Y)", pair, 0)) { value->x = pair[0]; value->y = pair[1]; }
    igSameLine(0, 18);
    pair[0] = value->width; pair[1] = value->height;
    igSetNextItemWidth(132);
    if (igInputInt2("Size (W, H)", pair, 0)) { value->width = pair[0]; value->height = pair[1]; }

    igSetNextItemWidth(105);
    igInputInt("First frame", &value->first_frame, 1, 0, 0);
    igSameLine(0, 18); igBeginDisabled(value->through_end);
    igSetNextItemWidth(105);
    igInputInt("Last frame", &value->last_frame, 1, 0, 0);
    igEndDisabled();
    igSameLine(0, 18); dialog_checkbox("Through end of scene", &value->through_end);
    dialog_checkbox("Scroll during playback", &value->scrolling);
}
int caption_dialog_draw(MarcusCaptionDialog *dialog, MarcusPlayer *player, int width, int height)
{
    static const char *title = "2D text###MarcusCaption";
    char label[96];
    int index, selected, open, submit, remove, changed;
    if (!caption_dialog_visible(dialog)) return 0;
    open = dialog->open;
    if (!dialog_begin(title, &open, width, height, 680, 560))
    {
        if (!open) close_dialog(dialog);
        return 0;
    }
    changed = 0;
    if (igBeginChild_Str("Text box options", dialog_vec(0, -64), 0, 0))
    {
        selected = dialog->selected;
        box_label(dialog->list, selected, label, sizeof(label));
        igSetNextItemWidth(440);
        if (igBeginCombo("Text box", label, 0))
        {
            for (index = -1; index < marcus_caption_count(dialog->list); ++index)
            {
                igPushID_Int(index);
                box_label(dialog->list, index, label, sizeof(label));
                if (igSelectable_Bool(label, index == selected, 0, dialog_vec(0, 0))) select_caption(dialog, index);
                if (index == selected) igSetItemDefaultFocus();
                igPopID();
            }
            igEndCombo();
        }
        igPushID_Int(dialog->selected);
        igTextUnformatted("Text", NULL);
        if (dialog->focus_text) { igSetKeyboardFocusHere(0); dialog->focus_text = 0; }
        igInputTextMultiline("##2D text", dialog->text, sizeof(dialog->text), dialog_vec(-1, 152), 0, NULL, NULL);
        igPopID();
        fields(&dialog->value);
    }
    igEndChild();
    igTextWrapped("%s", dialog->error);
    if (igButton("Cancel", dialog_vec(100, 0)) || igShortcut_Nil(ImGuiKey_Escape, 0)) open = 0;
    igSameLine(0, 10);
    igBeginDisabled(!dialog->text[0] && dialog->selected < 0);
    submit = igButton(dialog->selected < 0 ? "Add 2D text" : "Save changes", dialog_vec(130, 0));
    igEndDisabled();
    remove = 0;
    if (dialog->selected >= 0)
    {
        igSameLine(0, 10);
        remove = igButton("Delete box", dialog_vec(110, 0));
    }
    if (open && (submit || remove))
    {
        if (marcus_edit_caption(player, dialog->list, dialog->selected, &dialog->value, remove ? NULL : dialog->text))
        {
            changed = 1;
            open = 0;
        }
        else
            SDL_strlcpy(dialog->error, marcus_error(player), sizeof(dialog->error));
    }
    dialog_end(!open);
    if (!open) close_dialog(dialog);
    return changed;
}
