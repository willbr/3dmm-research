#include "dialog_host.h"
#include "dialog_widgets.h"
#include "movie_browser.h"
#include "background_browser.h"
#include "scene_organiser.h"
#include "camera_picker.h"
#include <math.h>
#include <string.h>

static void action(const MarcusDialogHost *host, void *model, int command, int index)
{ host->action(model, command, index, 0, NULL); }
static float fit_zoom(const MarcusDialogState *state, ImVec2 available)
{
    int columns, limit, rows;
    float horizontal, vertical, zoom, best;
    best = state->minimum_zoom;
    limit = (int)(available.x / 16);
    if (limit > state->count) limit = state->count;
    for (columns = 1; columns <= limit; ++columns) {
        rows = state->count / columns + (state->count % columns != 0);
        horizontal = (available.x - (columns - 1) * 2) / (columns * state->preview_width);
        vertical = (available.y - (rows - 1) * 2) / (rows * state->preview_height);
        zoom = horizontal < vertical ? horizontal : vertical;
        if (zoom > best) best = zoom;
    }
    return best > 1 ? 1 : best;
}
static void navigate(const MarcusDialogHost *host, void *model, MarcusDialogState *state, int columns, int rows)
{
    int next, step;
    if (igGetIO_Nil()->WantTextInput || igIsAnyItemActive() || !state->count ||
        igGetIO_Nil()->KeyAlt || igGetIO_Nil()->KeyCtrl || igGetIO_Nil()->KeySuper) return;
    next = state->selected;
    if (next < 0) next = 0;
    step = 0;
    if (igIsKeyPressed_Bool(ImGuiKey_LeftArrow, 1)) step = -1;
    if (igIsKeyPressed_Bool(ImGuiKey_RightArrow, 1)) step = 1;
    if (igIsKeyPressed_Bool(ImGuiKey_UpArrow, 1)) step = -columns;
    if (igIsKeyPressed_Bool(ImGuiKey_DownArrow, 1)) step = columns;
    if (igIsKeyPressed_Bool(ImGuiKey_PageUp, 1)) step = -columns * rows;
    if (igIsKeyPressed_Bool(ImGuiKey_PageDown, 1)) step = columns * rows;
    if (step > 0) next = step >= state->count - next ? state->count - 1 : next + step;
    else if (step < 0) next = next < -step ? 0 : next + step;
    if (igIsKeyPressed_Bool(ImGuiKey_Home, 0)) next = 0;
    if (igIsKeyPressed_Bool(ImGuiKey_End, 0)) next = state->count - 1;
    if (next < 0) next = 0;
    if (next >= state->count) next = state->count - 1;
    if (next != state->selected) { action(host, model, DIALOG_SELECT, next); state->selected = next; }
}
static void grid(const MarcusDialogHost *host, void *model, MarcusDialogState *state, SDL_Renderer *renderer)
{
    ImVec2 available, tile;
    ImGuiListClipper *clip;
    ImGuiStorage *storage;
    MarcusDialogItem item;
    char id[48];
    int columns, rows, row, column, index, first, last, previous, clicked, compact, total_rows;
    ImGuiID count_key, columns_key, filter_key;
    float width, picture_height, row_height, spacing;
    available = igGetContentRegionAvail(); spacing = igGetStyle()->ItemSpacing.x;
    compact = state->kind == DIALOG_FRAMES;
    if (compact && state->fit) {
        state->zoom = fit_zoom(state, available);
        host->action(model, DIALOG_ZOOM, 1, state->zoom, NULL);
    }
    width = compact ? state->preview_width * state->zoom : state->kind == DIALOG_MOVIES ? 176.0f : 212.0f;
    if (width < 16) width = 16;
    columns = (int)((available.x + spacing) / (width + spacing)); if (columns < 1) columns = 1;
    if (!compact) width = (available.x - spacing * (columns - 1)) / columns;
    picture_height = compact ? state->preview_height * state->zoom : width * .70f;
    tile = dialog_vec(width, picture_height);
    row_height = picture_height + (compact ? 0 : igGetTextLineHeightWithSpacing() * 2 + igGetStyle()->ItemSpacing.y) + igGetStyle()->ItemSpacing.y;
    if (row_height < 8) row_height = 8;
    rows = (int)(available.y / row_height); if (rows < 1) rows = 1;
    previous = state->selected; navigate(host, model, state, columns, rows);
    clip = ImGuiListClipper_ImGuiListClipper();
    if (!clip) return;
    storage = igGetStateStorage(); count_key = igGetID_Str("Grid count"); columns_key = igGetID_Str("Grid columns");
    filter_key = igGetID_Str("Grid filter");
    if (igIsWindowAppearing() || previous != state->selected ||
        ImGuiStorage_GetInt(storage, count_key, -1) != state->count ||
        ImGuiStorage_GetInt(storage, columns_key, -1) != columns ||
        ImGuiStorage_GetInt(storage, filter_key, -1) != state->filter)
        igSetScrollY_Float((float)(state->selected / columns) * row_height);
    ImGuiStorage_SetInt(storage, count_key, state->count); ImGuiStorage_SetInt(storage, columns_key, columns);
    ImGuiStorage_SetInt(storage, filter_key, state->filter);
    total_rows = state->count / columns + (state->count % columns != 0);
    ImGuiListClipper_Begin(clip, total_rows, row_height);
    while (ImGuiListClipper_Step(clip))
    {
        first = clip->DisplayStart * columns;
        last = clip->DisplayEnd > state->count / columns ? state->count : clip->DisplayEnd * columns;
        host->prepare(model, renderer, first, last);
        for (row = clip->DisplayStart; row < clip->DisplayEnd; ++row)
        {
            for (column = 0; column < columns; ++column)
            {
                if (column >= state->count - row * columns) break;
                index = row * columns + column;
                if (column) igSameLine(0, spacing);
                igPushID_Int(index); igBeginGroup();
                memset(&item, 0, sizeof(item)); host->item(model, renderer, index, &item);
                SDL_snprintf(id, sizeof(id), "##tile%d", index);
                clicked = dialog_thumbnail(id, item.texture, tile, index == state->selected,
                    item.directory ? "Folder" : item.preview_unavailable ? "Preview unavailable" : "Loading preview...");
                if (igIsItemHovered(0)) {
                    igSetTooltip("%s%s%s", item.label, item.detail[0] ? "\n" : "", item.detail);
                    if (igIsMouseDoubleClicked_Nil(0)) { action(host, model, DIALOG_SELECT, index); action(host, model, DIALOG_ACCEPT, index); }
                }
                if (clicked) { action(host, model, DIALOG_SELECT, index); state->selected = index; }
                if (!compact) {
                    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, dialog_vec(0, 0));
                    if (igBeginChild_Str("Caption", dialog_vec(width, igGetTextLineHeightWithSpacing() * 2), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                        igPushTextWrapPos(0); igTextUnformatted(item.label, NULL); igPopTextWrapPos();
                    }
                    igEndChild(); igPopStyleVar(1);
                }
                igEndGroup(); igPopID();
            }
        }
    }
    ImGuiListClipper_destroy(clip);
}
static void list(const MarcusDialogHost *host, void *model, MarcusDialogState *state, SDL_Renderer *renderer)
{
    ImGuiListClipper *clip;
    MarcusDialogItem item;
    int i, previous;
    clip = ImGuiListClipper_ImGuiListClipper(); if (!clip) return;
    previous = state->selected;
    navigate(host, model, state, 1, 12);
    if (igIsWindowAppearing() || previous != state->selected)
        igSetScrollY_Float(state->selected * igGetTextLineHeightWithSpacing());
    ImGuiListClipper_Begin(clip, state->count, igGetTextLineHeightWithSpacing());
    while (ImGuiListClipper_Step(clip)) {
        host->prepare(model, renderer, clip->DisplayStart, clip->DisplayEnd);
        for (i = clip->DisplayStart; i < clip->DisplayEnd; ++i) {
            memset(&item, 0, sizeof(item)); if (!host->item(model, renderer, i, &item)) continue;
            igPushID_Int(i);
            if (igSelectable_Bool(item.label, i == state->selected, ImGuiSelectableFlags_AllowDoubleClick, dialog_vec(0, 0))) {
                action(host, model, DIALOG_SELECT, i); state->selected = i;
                if (igIsMouseDoubleClicked_Nil(0)) action(host, model, DIALOG_ACCEPT, i);
            }
            igPopID();
        }
    }
    ImGuiListClipper_destroy(clip);
}
static void movie_controls(const MarcusDialogHost *host, void *model, MarcusDialogState *s)
{
    static const char *filters[] = {"All movies", "3MM", "VMM", "ZIP"};
    int changed, modifier;
    modifier = igGetIO_Nil()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
    if (igShortcut_Nil(ImGuiMod_Alt | ImGuiKey_LeftArrow, 0)) action(host, model, DIALOG_BACK, 0);
    if (igShortcut_Nil(ImGuiMod_Alt | ImGuiKey_UpArrow, 0)) action(host, model, DIALOG_UP, 0);
    if (igShortcut_Nil(ImGuiKey_F5, 0)) action(host, model, DIALOG_REFRESH, 0);
    igBeginDisabled(!s->can_back); if (igButton("Back", dialog_vec(64, 0))) action(host, model, DIALOG_BACK, 0); igEndDisabled();
    igSameLine(0, 8); if (igButton("Up", dialog_vec(48, 0))) action(host, model, DIALOG_UP, 0);
    igSameLine(0, 8); igSetNextItemWidth(-70);
    if (igShortcut_Nil(modifier | ImGuiKey_L, 0)) igSetKeyboardFocusHere(0);
    changed = igInputText("##Location", s->address, sizeof(s->address), ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL);
    if (changed || igIsItemEdited()) host->action(model, DIALOG_ADDRESS, 0, 0, s->address);
    igSameLine(0, 8); if (igButton("Go", dialog_vec(60, 0)) || changed) action(host, model, DIALOG_GO, 0);
    igSetNextItemWidth(-350);
    if (igShortcut_Nil(modifier | ImGuiKey_F, 0)) igSetKeyboardFocusHere(0);
    if (igInputTextWithHint("##Search movies", "Search movies", s->search, sizeof(s->search), 0, NULL, NULL))
        host->action(model, DIALOG_SEARCH, 0, 0, s->search);
    igSameLine(0, 8); igSetNextItemWidth(130);
    if (igCombo_Str_arr("##Movie type", &s->filter, filters, 4, -1)) action(host, model, DIALOG_FILTER, s->filter);
    igSameLine(0, 8); if (igButton(s->sort_date ? "Newest first" : "Name A-Z", dialog_vec(110, 0))) action(host, model, DIALOG_SORT, 0);
    igSameLine(0, 8); if (igButton(s->grid ? "List" : "Cards", dialog_vec(80, 0))) action(host, model, DIALOG_GRID, 0);
}
static void favourites(const MarcusDialogHost *host, void *model, MarcusDialogState *s, SDL_Renderer *renderer)
{
    MarcusDialogItem item;
    int i;
    if (igButton("Recent movies", dialog_vec(-1, 0))) action(host, model, DIALOG_RECENT, 0);
    if (igButton("Movie folder", dialog_vec(-1, 0))) action(host, model, DIALOG_MOVIE_FOLDER, 0);
    if (igButton("Home", dialog_vec(-1, 0))) action(host, model, DIALOG_HOME, 0);
    igSeparatorText("Favourites");
    igBeginDisabled(!s->is_favourite && s->favourite_count >= 12);
    if (igButton(s->is_favourite ? "Remove favourite" : "Add favourite", dialog_vec(-1, 0))) action(host, model, DIALOG_FAVOURITE, 0);
    igEndDisabled();
    for (i = 0; i < s->favourite_count; ++i) {
        memset(&item, 0, sizeof(item)); if (!host->item(model, renderer, -i - 1, &item)) continue;
        igPushID_Int(i);
        if (igSelectable_Bool(item.label, 0, 0, dialog_vec(0, 0))) action(host, model, DIALOG_FAVOURITE_GO, i);
        if (igIsItemHovered(0)) igSetTooltip("%s", item.detail);
        igPopID();
    }
}
static void details(const MarcusDialogHost *host, void *model, MarcusDialogState *s, SDL_Renderer *renderer)
{
    MarcusDialogItem item;
    ImVec2 available;
    memset(&item, 0, sizeof(item));
    if (s->selected < 0) return;
    if (!host->item(model, renderer, s->selected, &item)) return;
    igTextWrapped("%s", item.label); available = igGetContentRegionAvail();
    dialog_image(item.texture, dialog_vec(available.x, available.x * .65f));
    igTextWrapped("%s", item.detail);
    if (s->kind == DIALOG_MOVIES && !item.directory) {
        igBeginDisabled(s->opening);
        if (igButton(s->calculating ? "Cancel calculation" : "Calculate duration", dialog_vec(-1, 0))) action(host, model, DIALOG_CALCULATE, 0);
        igEndDisabled();
    }
}
static void draw_picker(void *model, const MarcusDialogHost *host, MarcusUI *ui, int width, int height, const char *identity)
{
    MarcusDialogState s;
    int open, movie, split, done, disabled, confirm;
    float zoom;
    char label[160];
    host->prepare(model, ui->renderer, -1, -1); memset(&s, 0, sizeof(s)); host->state(model, &s);
    if (!s.open) { dialog_close(identity); return; }
    open = 1; movie = s.kind == DIALOG_MOVIES; split = movie || s.kind == DIALOG_BACKGROUNDS;
    if (!dialog_begin(identity, &open, width, height, movie ? 1400 : 0, 0)) { if (!open) action(host, model, DIALOG_CANCEL, 0); return; }
    igTextUnformatted(s.title, NULL);
    if (movie) movie_controls(host, model, &s);
    if (s.kind == DIALOG_MODELS) {
        if (igRadioButton_Bool("All", s.filter == 0)) action(host, model, DIALOG_FILTER, 0);
        igSameLine(0, 16);
        if (igRadioButton_Bool("Actors", s.filter == 1)) action(host, model, DIALOG_FILTER, 1);
        igSameLine(0, 16);
        if (igRadioButton_Bool("Props", s.filter == 2)) action(host, model, DIALOG_FILTER, 2);
    }
    if (s.kind == DIALOG_FRAMES) {
        if (dialog_checkbox("Whole movie", &s.whole_movie)) action(host, model, DIALOG_SCOPE, 0);
        igSameLine(0, 16); igSetNextItemWidth(180); zoom = s.zoom;
        if (igSliderFloat("Thumbnail size", &zoom, s.minimum_zoom, 1, "%.2fx", 0)) host->action(model, DIALOG_ZOOM, 0, zoom, NULL);
        igSameLine(0, 16);
        if (igButton("Fit", dialog_vec(64, 0))) action(host, model, DIALOG_FIT, 0);
        if (!igIsAnyItemActive()) {
            zoom = s.zoom;
            if (igShortcut_Nil(ImGuiKey_Minus, ImGuiInputFlags_Repeat)) zoom /= 1.25f;
            if (igShortcut_Nil(ImGuiKey_Equal, ImGuiInputFlags_Repeat)) zoom *= 1.25f;
            if (igGetIO_Nil()->KeyCtrl && igGetIO_Nil()->MouseWheel != 0)
                zoom *= (float)pow(1.25, igGetIO_Nil()->MouseWheel);
            if (zoom != s.zoom) host->action(model, DIALOG_ZOOM, 0, zoom, NULL);
        }
    }
    host->state(model, &s);
    if (igBeginChild_Str("Dialog contents", dialog_vec(0, -86), 0, 0)) {
        if (split && igBeginTable("Browse and preview", movie ? 3 : 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp, dialog_vec(0, 0), 0)) {
            if (movie) igTableSetupColumn("Locations", ImGuiTableColumnFlags_WidthFixed, 172, 0);
            igTableSetupColumn("Items", ImGuiTableColumnFlags_WidthStretch, 2, 0);
            igTableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 1, 0);
            if (movie) {
                igTableNextColumn(); if (igBeginChild_Str("Locations", dialog_vec(0, 0), 0, 0)) favourites(host, model, &s, ui->renderer); igEndChild();
            }
            igTableNextColumn();
            if (igBeginChild_Str("Items", dialog_vec(0, 0), 0, 0)) {
                if (movie && s.grid) grid(host, model, &s, ui->renderer); else list(host, model, &s, ui->renderer);
            }
            igEndChild(); igTableNextColumn();
            if (igBeginChild_Str("Preview", dialog_vec(0, 0), 0, 0)) details(host, model, &s, ui->renderer);
            igEndChild(); igEndTable();
        }
        else if (!split) {
            if (s.kind == DIALOG_FRAMES) {
                igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, dialog_vec(2, 2));
                igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, dialog_vec(1, 1));
            }
            grid(host, model, &s, ui->renderer);
            if (s.kind == DIALOG_FRAMES) igPopStyleVar(2);
        }
    }
    igEndChild(); host->state(model, &s);
    if (movie) {
        igSetNextItemWidth(-1);
        done = igInputText("##Movie filename", s.filename, sizeof(s.filename), ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL);
        host->action(model, DIALOG_FILENAME, 0, 0, s.filename);
        if (done) action(host, model, DIALOG_ACCEPT, 1);
    }
    igTextWrapped("%s", s.error[0] ? s.error : s.status); igSeparator();
    if (igButton("Cancel", dialog_vec(100, 0)) || igShortcut_Nil(ImGuiKey_Escape, 0)) action(host, model, DIALOG_CANCEL, 0);
    igSameLine(0, 8); disabled = s.loading || s.opening || (s.selected < 0 && (!movie || !s.filename[0]));
    igBeginDisabled(disabled);
    SDL_snprintf(label, sizeof(label), "%s###ConfirmDialog", s.opening ? "Opening..." : s.confirm);
    confirm = igButton(label, dialog_vec(160, 0));
    if (!disabled && (confirm || (!igIsAnyItemActive() && !igGetIO_Nil()->WantTextInput && igShortcut_Nil(ImGuiKey_Enter, 0))))
        action(host, model, DIALOG_ACCEPT, 0);
    igEndDisabled();
    if (s.kind == DIALOG_CAMERAS && s.storyboard) {
        igSameLine(0, 12); if (igButton("Edit storyboard", dialog_vec(144, 0))) action(host, model, DIALOG_EDIT_BOARD, 0);
        igSameLine(0, 8); if (igButton("New angle", dialog_vec(120, 0))) action(host, model, DIALOG_NEW_ANGLE, 0);
    }
    host->state(model, &s); if (!open) action(host, model, DIALOG_CANCEL, 0);
    dialog_end(!open || !s.open);
}
int browser_event(MarcusBrowser *b, const SDL_Event *e)
{
    if (!browser_visible(b)) return 0;
    if (e->type == SDL_EVENT_DROP_FILE) marcus_movie_dialog_host.action(b, DIALOG_DROP, 0, 0, e->drop.data);
    return dialog_input_event(e);
}
void browser_draw(MarcusBrowser *b, MarcusUI *ui, int w, int h)
{ if (b) draw_picker(b, &marcus_movie_dialog_host, ui, w, h, "Open movie###MarcusMovieBrowser"); }
int background_browser_event(MarcusBackgroundBrowser *b, const SDL_Event *e, int w, int h)
{ (void)w; (void)h; return background_browser_visible(b) && dialog_input_event(e); }
void background_browser_draw(MarcusBackgroundBrowser *b, MarcusUI *ui, int w, int h)
{ if (b) draw_picker(b, &marcus_background_dialog_host, ui, w, h, "Choose content###MarcusAssets"); }
int scene_organiser_event(MarcusSceneOrganiser *s, const SDL_Event *e, int w, int h)
{ (void)w; (void)h; return scene_organiser_visible(s) && dialog_input_event(e); }
void scene_organiser_draw(MarcusSceneOrganiser *s, MarcusUI *ui, int w, int h)
{ if (s) draw_picker(s, &marcus_scene_dialog_host, ui, w, h, "Scene and frame organiser###MarcusOrganiser"); }
int camera_picker_event(MarcusCameraPicker *p, const SDL_Event *e, int w, int h)
{ (void)w; (void)h; return camera_picker_visible(p) && dialog_input_event(e); }
void camera_picker_draw(MarcusCameraPicker *p, MarcusUI *ui, int w, int h)
{ if (p) draw_picker(p, &marcus_camera_dialog_host, ui, w, h, "Camera angles###MarcusCameras"); }
