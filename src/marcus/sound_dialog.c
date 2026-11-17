/* Sound authoring UI. All application state and widgets use C89 + cimgui. */
#include "sound_dialog.h"
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
#include <stdlib.h>
#include <string.h>

typedef struct SoundPick
{
    SDL_AtomicInt references;
    SDL_Mutex *mutex;
    int done, failed;
    char path[4096];
} SoundPick;
struct MarcusSoundDialog
{
    MarcusPlayer *player;
    SDL_Window *window;
    SoundPick *pick;
    MarcusSound original;
    int open, opening, selected, frame, style, loop, queue, grouped;
    int layout_width, layout_height, changed, chosen;
    int toolbar_drawn, toolbar_visible;
    SDL_FRect toolbar_rect;
    unsigned long document;
    float volume;
    char path[4096], search[192], error[1024];
    MarcusWaveformColumn waveform[512];
    MarcusWaveformInfo audio;
    int waveform_count;
};
static ImVec2 vec(float x, float y) { ImVec2 v; v.x = x; v.y = y; return v; }
static void release_pick(SoundPick *pick)
{
    if (pick && SDL_AtomicDecRef(&pick->references)) { SDL_DestroyMutex(pick->mutex); free(pick); }
}
static void SDLCALL picked(void *context, const char *const *files, int filter)
{
    SoundPick *pick;
    (void)filter;
    pick = (SoundPick *)context;
    SDL_LockMutex(pick->mutex);
    pick->failed = !files;
    if (files && files[0]) SDL_strlcpy(pick->path, files[0], sizeof(pick->path));
    pick->done = 1;
    SDL_UnlockMutex(pick->mutex);
    release_pick(pick);
}
static void browse(MarcusSoundDialog *d)
{
    static const SDL_DialogFileFilter filters[] = {{"Audio files", "wav;mp3;mid;midi"}, {"All files", "*"}};
    SoundPick *pick;
    if (d->pick) return;
    pick = (SoundPick *)calloc(1, sizeof(*pick));
    if (!pick) return;
    pick->mutex = SDL_CreateMutex();
    if (!pick->mutex) { free(pick); return; }
    SDL_SetAtomicInt(&pick->references, 2); d->pick = pick;
    SDL_ShowOpenFileDialog(picked, pick, d->window, filters, 2, d->path[0] ? d->path : NULL, 0);
}
static void poll_pick(MarcusSoundDialog *d)
{
    SoundPick *pick;
    const char *extension;
    int done;
    pick = d->pick;
    if (!pick) return;
    SDL_LockMutex(pick->mutex); done = pick->done;
    if (done)
    {
        if (pick->failed) SDL_strlcpy(d->error, "The audio file chooser could not be opened.", sizeof(d->error));
        else if (pick->path[0])
        {
            SDL_strlcpy(d->path, pick->path, sizeof(d->path));
            extension = strrchr(d->path, '.');
            if (extension && (!SDL_strcasecmp(extension, ".mid") || !SDL_strcasecmp(extension, ".midi"))) d->style = MARCUS_SOUND_MUSIC;
            else if (d->style == MARCUS_SOUND_MUSIC) d->style = MARCUS_SOUND_EFFECT;
            d->error[0] = 0;
        }
    }
    SDL_UnlockMutex(pick->mutex);
    if (done) { d->pick = NULL; release_pick(pick); }
}
MarcusSoundDialog *sound_dialog_create(void)
{
    MarcusSoundDialog *d;
    d = (MarcusSoundDialog *)calloc(1, sizeof(*d));
    if (d) d->selected = d->chosen = -1;
    return d;
}
void sound_dialog_destroy(MarcusSoundDialog *d)
{
    if (!d) return;
    release_pick(d->pick); free(d);
}
int sound_dialog_visible(const MarcusSoundDialog *d) { return d && d->open; }
static void close_dialog(MarcusSoundDialog *d)
{
    if (d->player) marcus_stop_sound_preview(d->player);
    d->open = 0; release_pick(d->pick); d->pick = NULL;
}
static void select_sound(MarcusSoundDialog *d, int index)
{
    MarcusInfo info;
    MarcusSound other;
    MarcusWaveformColumn peak;
    int i;
    marcus_stop_sound_preview(d->player);
    d->selected = index; d->path[0] = d->error[0] = 0; d->waveform_count = d->grouped = 0;
    marcus_info(d->player, &info);
    if (index >= 0 && marcus_sound(d->player, index, &d->original))
    {
        d->volume = d->original.volume * 100.0f / 65536;
        d->style = (int)d->original.style; d->loop = d->original.loop; d->queue = d->original.queue;
        d->frame = (int)d->original.origin_frame;
        for (i = 0; i < info.sound_count; ++i) if (marcus_sound(d->player, i, &other) && !other.actor_sound &&
            other.origin_scene == d->original.origin_scene && other.origin_frame == d->original.origin_frame && other.style == d->original.style)
            ++d->grouped;
        if (marcus_sound_waveform(d->player, index, 0, 1, 1, &peak, &d->audio) && d->audio.seconds > 0 &&
            marcus_sound_waveform(d->player, index, 0, d->audio.seconds / 512, 512, d->waveform, &d->audio)) d->waveform_count = 512;
    }
    else
    {
        d->selected = -1; d->volume = 100; d->style = MARCUS_SOUND_EFFECT; d->loop = 0; d->queue = 1;
        d->frame = (int)(info.frame - info.first_frame + 1);
    }
}
static void show(MarcusSoundDialog *d, MarcusPlayer *player, SDL_Window *window, int selected)
{
    d->player = player; d->window = window; d->document = marcus_document(player);
    d->open = d->opening = 1; d->search[0] = 0; select_sound(d, selected);
}
void sound_dialog_toolbar(MarcusSoundDialog *d, MarcusPlayer *player, SDL_Window *window, int selected,
                          float x, float y, int enabled)
{
    int add, edit;
    if (!d || !igGetCurrentContext()) return;
    d->toolbar_drawn = 1;
    d->toolbar_rect.x = x; d->toolbar_rect.y = y; d->toolbar_rect.w = 286; d->toolbar_rect.h = 48;
    igSetNextWindowPos(vec(x, y), ImGuiCond_Always, vec(0, 0));
    igSetNextWindowSize(vec(286, 48), ImGuiCond_Always);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, vec(0, 0));
    igBegin("##Sound authoring tools", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    igBeginDisabled(!enabled);
    add = igButton("Add sound...", vec(136, 44));
    igSameLine(0, 8); igBeginDisabled(selected < 0);
    edit = igButton("Edit sound...", vec(136, 44));
    igEndDisabled(); igEndDisabled(); igEnd(); igPopStyleVar(1);
    if (add || edit) show(d, player, window, add ? -1 : selected);
}
int sound_dialog_event(MarcusSoundDialog *d, const SDL_Event *event)
{
    float x, y;
    if (!d) return 0;
    if (!d->open)
    {
        if (!d->toolbar_visible) return 0;
        if (event->type == SDL_EVENT_MOUSE_MOTION) { x = event->motion.x; y = event->motion.y; }
        else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
        { x = event->button.x; y = event->button.y; }
        else return 0;
        return x >= d->toolbar_rect.x && x < d->toolbar_rect.x + d->toolbar_rect.w &&
            y >= d->toolbar_rect.y && y < d->toolbar_rect.y + d->toolbar_rect.h;
    }
    switch (event->type)
    {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL: return 1;
    default: return 0;
    }
}
static int checkbox(const char *label, int *value)
{
    ImGuiStorage *storage;
    ImGuiID key;
    int changed;
    storage = igGetStateStorage(); key = igGetID_Str(label);
    ImGuiStorage_SetBool(storage, key, *value);
    changed = igCheckbox(label, ImGuiStorage_GetBoolRef(storage, key, *value));
    *value = ImGuiStorage_GetBool(storage, key, *value); return changed;
}
static void waveform(MarcusSoundDialog *d)
{
    ImVec2 size, start, a, b;
    ImDrawList *draw;
    int i;
    float x, center, scale;
    if (!d->waveform_count) return;
    size = igGetContentRegionAvail(); size.y = 120;
    start = igGetCursorScreenPos(); igDummy(size); draw = igGetWindowDrawList();
    ImDrawList_AddRectFilled(draw, start, vec(start.x + size.x, start.y + size.y), 0xff211c18U, 0, 0);
    center = start.y + size.y * .5f; scale = size.y * .45f * d->volume / 100;
    for (i = 0; i < d->waveform_count; ++i)
    {
        x = start.x + i * size.x / d->waveform_count;
        a = vec(x, center - d->waveform[i].maximum * scale); b = vec(x, center - d->waveform[i].minimum * scale);
        ImDrawList_AddLine(draw, a, b, 0xffc4cb78U, 1);
    }
    igText("%.3f seconds", d->audio.seconds);
}
static int find_edited(MarcusSoundDialog *d, const MarcusSoundOptions *options)
{
    MarcusInfo info;
    MarcusScene scene;
    MarcusSound sound;
    int i;
    marcus_info(d->player, &info);
    marcus_scene(d->player, d->original.origin_scene, &scene);
    for (i = 0; i < info.sound_count; ++i) if (marcus_sound(d->player, i, &sound) &&
        sound.actor_sound == d->original.actor_sound && sound.actor_id == d->original.actor_id &&
        sound.origin_scene == d->original.origin_scene && sound.child == d->original.child &&
        sound.style == options->style && sound.origin_frame == options->frame - scene.first_frame + 1) return i;
    return -1;
}
static void editor(MarcusSoundDialog *d)
{
    MarcusInfo info;
    MarcusScene scene;
    MarcusSoundOptions options;
    int changed, selected, existing, actor, valid, maximum;
    existing = d->selected >= 0; actor = existing && d->original.actor_sound;
    marcus_info(d->player, &info);
    marcus_scene(d->player, existing ? d->original.origin_scene : info.scene, &scene);
    maximum = (int)(scene.last_frame - scene.first_frame + 1);
    if (existing)
    {
        igTextWrapped("%s", d->original.name);
        igTextDisabled("%s", d->original.owner);
        if (d->original.carried) igText("Starts in scene %d", d->original.origin_scene + 1);
    }
    else
    {
        igText("Import audio");
        igSetNextItemWidth(-110);
        igInputText("##Audio file", d->path, sizeof(d->path), 0, NULL, NULL);
        igSameLine(0, 8); igBeginDisabled(d->pick != NULL);
        if (igButton("Browse...", vec(100, 0))) browse(d);
        igEndDisabled(); igTextDisabled("WAV / MP3 for effects and speech; MIDI for music");
    }
    igSeparator(); igBeginDisabled(actor || (existing && d->original.style == MARCUS_SOUND_MUSIC));
    igRadioButton_IntPtr("Effect", &d->style, MARCUS_SOUND_EFFECT); igSameLine(0, 16);
    igRadioButton_IntPtr("Speech", &d->style, MARCUS_SOUND_SPEECH); igEndDisabled(); igSameLine(0, 16);
    igBeginDisabled(actor || (existing && d->original.style != MARCUS_SOUND_MUSIC));
    igRadioButton_IntPtr("Music (MIDI)", &d->style, MARCUS_SOUND_MUSIC);
    igEndDisabled(); igBeginDisabled(actor);
    igText("Start frame"); igSetNextItemWidth(-1);
    igDragInt("##Sound start frame", &d->frame, 1, 1, maximum, "%d", ImGuiSliderFlags_AlwaysClamp);
    igEndDisabled();
    if (actor) igTextDisabled(d->original.motion ? "Timing follows the actor's action and matching cel." : "Timing follows the actor's animation.");
    igText("Volume"); igSetNextItemWidth(-1); igSliderFloat("##Sound volume", &d->volume, 0, 100, "%.0f%%", 0);
    if (checkbox("Loop", &d->loop) && d->loop) d->queue = 0;
    if (!existing)
    {
        igBeginDisabled(d->loop); checkbox("Queue after sounds at this frame", &d->queue); igEndDisabled();
    }
    else if (!actor && d->grouped > 1) igTextWrapped("Frame, volume and loop apply to this queued group. Remove deletes only this sound.");
    waveform(d);
    valid = existing || d->path[0];
    igBeginDisabled(!valid);
    if (igButton("Preview", vec(100, 0)))
    {
        changed = existing ? marcus_audition_sound(d->player, d->selected, (long)(d->volume * 65536 / 100 + .5f)) :
            marcus_audition_sound_file(d->player, d->path, d->style, (long)(d->volume * 65536 / 100 + .5f));
        if (!changed) SDL_strlcpy(d->error, marcus_error(d->player), sizeof(d->error));
    }
    igEndDisabled(); igSameLine(0, 8);
    if (igButton("Stop preview", vec(120, 0))) marcus_stop_sound_preview(d->player);
    igSeparator();
    options.frame = scene.first_frame + d->frame - 1;
    options.volume = (long)(d->volume * 65536 / 100 + .5f);
    options.style = d->style; options.loop = d->loop; options.queue = d->queue;
    igBeginDisabled(!valid);
    if (igButton(existing ? "Apply changes" : "Add sound", vec(150, 0)))
    {
        selected = -1;
        changed = existing ? marcus_set_sound(d->player, d->selected, &options) :
            (selected = marcus_add_sound(d->player, d->path, &options)) >= 0;
        if (!changed) SDL_strlcpy(d->error, marcus_error(d->player), sizeof(d->error));
        else
        {
            if (existing) selected = find_edited(d, &options);
            if (d->document != marcus_document(d->player)) { d->changed = 1; d->chosen = selected; }
            d->document = marcus_document(d->player); select_sound(d, selected);
        }
    }
    igEndDisabled();
    if (existing)
    {
        igSameLine(0, 8);
        if (igButton("Remove sound", vec(130, 0)))
        {
            if (!marcus_remove_sound(d->player, d->selected)) SDL_strlcpy(d->error, marcus_error(d->player), sizeof(d->error));
            else { d->document = marcus_document(d->player); d->changed = 1; d->chosen = -1; select_sound(d, -1); }
        }
    }
    if (d->error[0]) igTextWrapped("%s", d->error);
}
void sound_dialog_draw(MarcusSoundDialog *d, MarcusPlayer *player, MarcusUI *ui, int width, int height)
{
    ImGuiStorage *storage;
    ImGuiID key;
    ImVec2 size;
    MarcusInfo info;
    MarcusSound sound;
    char label[512];
    int i;
    (void)ui;
    if (!d) return;
    d->toolbar_visible = d->toolbar_drawn; d->toolbar_drawn = 0;
    if (!d->open) return;
    if (d->player != player || d->document != marcus_document(player))
    {
        if (d->player != player) d->player = NULL;
        if (igBeginPopupModal("Sounds###MarcusSounds", NULL, ImGuiWindowFlags_NoSavedSettings)) { igCloseCurrentPopup(); igEndPopup(); }
        close_dialog(d); return;
    }
    poll_pick(d); marcus_info(player, &info);
    size = vec((float)width - 32, (float)height - 32);
    if (size.x > 1100) size.x = 1100;
    if (size.y > 820) size.y = 820;
    if (d->opening) igOpenPopup_Str("Sounds###MarcusSounds", 0);
    if (d->opening || width != d->layout_width || height != d->layout_height)
    {
        igSetNextWindowPos(vec(width * .5f, height * .5f), ImGuiCond_Always, vec(.5f, .5f));
        d->layout_width = width; d->layout_height = height;
    }
    igSetNextWindowSize(size, ImGuiCond_Always);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, vec(16, 14));
    igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, vec(10, 10));
    storage = igGetStateStorage(); key = igGetID_Str("MarcusSoundVisible"); ImGuiStorage_SetBool(storage, key, 1);
    if (igBeginPopupModal("Sounds###MarcusSounds", ImGuiStorage_GetBoolRef(storage, key, 1),
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        d->opening = 0;
        if (igBeginChild_Str("Sound content", vec(0, -52), 0, 0))
        {
            if (igBeginTable("Sound list and editor", 2, ImGuiTableFlags_SizingStretchProp, vec(0, 0), 0))
            {
                igTableSetupColumn("Sounds", ImGuiTableColumnFlags_WidthStretch, 2, 0);
                igTableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 3, 0);
                igTableNextColumn();
                if (igBeginChild_Str("Scene sounds", vec(0, 0), 0, 0))
                {
                    if (igButton("+ Add sound...", vec(-1, 0))) select_sound(d, -1);
                    igSetNextItemWidth(-1); igInputTextWithHint("##Find sound", "Find sound", d->search, sizeof(d->search), 0, NULL, NULL);
                    for (i = 0; i < info.sound_count; ++i) if (marcus_sound(player, i, &sound) &&
                        (!d->search[0] || SDL_strcasestr(sound.name, d->search) || SDL_strcasestr(sound.owner, d->search)))
                    {
                        SDL_snprintf(label, sizeof(label), "%s / %s##sound%d", sound.name, sound.owner, i);
                        if (igSelectable_Bool(label, d->selected == i, 0, vec(0, 0))) select_sound(d, i);
                    }
                    if (!info.sound_count) igTextWrapped("No sounds in this scene. Import an audio file to add one.");
                }
                igEndChild(); igTableNextColumn();
                if (igBeginChild_Str("Sound details", vec(0, 0), 0, 0)) editor(d);
                igEndChild(); igEndTable();
            }
        }
        igEndChild(); igSeparator();
        if (igButton("Close", vec(100, 0)) || igShortcut_Nil(ImGuiKey_Escape, 0)) close_dialog(d);
        if (!d->open) igCloseCurrentPopup();
        igEndPopup();
    }
    if (!ImGuiStorage_GetBool(storage, key, 1)) close_dialog(d);
    igPopStyleVar(2);
}
int sound_dialog_take(MarcusSoundDialog *d, int *selected)
{
    if (!d || !d->changed) return 0;
    if (selected) *selected = d->chosen;
    d->changed = 0; return 1;
}
