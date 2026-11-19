#include "ribbon.h"
#include "dialog_widgets.h"

typedef struct RibbonTool
{
    int action, icon;
    const char *label, *hint;
    unsigned long colour;
} RibbonTool;
static const RibbonTool scene_tools[] = {
    {RIBBON_NEW_SCENE, 0, "New scene", "Choose a background and add a new scene", 0x78cbc4},
    {RIBBON_SCENES, 1, "Scenes", "Browse the movie's scenes", 0xe9bc75},
    {RIBBON_FRAMES, 1, "Frames", "Browse scene frames", 0x96aee6},
    {RIBBON_CAMERAS, 9, "Camera angles", "Choose a camera angle at this frame", 0xe9bc75},
    {RIBBON_CAMERA_GUIDES, 9, "All cameras", "Show all camera guides or only the active camera in the work view", 0x78cbc4},
    {RIBBON_STORYBOARD, 8, "Storyboard", "Paint this storyboard or create a new storyboard scene", 0xb3a4d6},
    {RIBBON_PREVIOUS, 2, "Previous", "Go to the previous scene", 0xaeb6c0},
    {RIBBON_NEXT, 3, "Next", "Go to the next scene", 0xaeb6c0}};
static const RibbonTool actor_tools[] = {
    {RIBBON_NEW_OBJECT, 11, "Add object", "Choose an actor or prop to add at the current frame", 0xe9bc75},
    {RIBBON_COPY_OBJECT, 4, "Copy object", "Copy the selected object (Ctrl+C / Cmd+C)", 0x78cbc4},
    {RIBBON_PASTE_OBJECT, 4, "Paste object", "Paste a new object at the playhead (Ctrl+V / Cmd+V)", 0x78cbc4},
    {RIBBON_OBJECTS, 4, "Objects", "Show object tracks", 0x78cbc4},
    {RIBBON_ACTOR, 4, "Next actor", "Select the next actor in this scene", 0xe9bc75},
    {RIBBON_PROP, 5, "Next prop", "Select the next prop in this scene", 0xb3a4d6},
    {RIBBON_BOTH, 1, "All tracks", "Show object and sound tracks together", 0x96aee6}};
static const RibbonTool sound_tools[] = {
    {RIBBON_SOUNDS, 6, "Sound tracks", "Show the scene's sound tracks", 0x96aee6},
    {RIBBON_BOTH, 1, "All tracks", "Show object and sound tracks together", 0x78cbc4},
    {RIBBON_MUTE, 6, "Mute", "Toggle movie audio", 0xe9bc75},
    {RIBBON_RESET_MIX, 7, "Reset mix", "Reset sound track mute and solo choices", 0xaeb6c0}};
static const RibbonTool word_tools[] = {
    {RIBBON_NEW_CAPTION, 12, "New 2D text", "Add a flat text box at the current frame", 0xb3a4d6},
    {RIBBON_EDIT_CAPTION, 8, "Edit 2D text", "Edit the current scene's 2D text boxes", 0x78cbc4},
    {RIBBON_NEW_TEXT, 12, "New 3D text", "Create 3D text using an original font and shape", 0xb3a4d6},
    {RIBBON_EDIT_TEXT, 8, "Edit 3D text", "Edit the selected text's wording, font and shape", 0x78cbc4},
    {RIBBON_WORDS, 8, "3D words", "Select the next 3D text object", 0xe9bc75},
    {RIBBON_OBJECTS, 4, "Objects", "Show object tracks and properties", 0x78cbc4}};
static const RibbonTool *tools_for(int tab, int *count)
{
    if (tab == 1) { *count = sizeof(actor_tools) / sizeof(actor_tools[0]); return actor_tools; }
    if (tab == 2) { *count = sizeof(sound_tools) / sizeof(sound_tools[0]); return sound_tools; }
    if (tab == 3) { *count = sizeof(word_tools) / sizeof(word_tools[0]); return word_tools; }
    *count = sizeof(scene_tools) / sizeof(scene_tools[0]); return scene_tools;
}
int ribbon_draw(MarcusUI *ui, int width, int *tab, const char *name, int loaded, int modified, int muted,
    unsigned long cameras, int show_cameras, int selected_text, int can_copy, int can_paste)
{
    static const char *tabs[] = {"Scenes", "Actors & props", "Sounds", "Words"};
    static int displayed_tab = -1;
    const RibbonTool *tools;
    const char *label;
    int action, index, count, enabled, active, requested, external, modifier;
    action = RIBBON_NONE;
    (void)ui;
    if (ui_panel("Editor ribbon", 0, 0, (float)width, MARCUS_RIBBON_HEIGHT, 0, 0)) {
        if (!igGetIO_Nil()->WantTextInput && !igIsAnyItemActive() &&
            !igIsPopupOpen_Str(NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            modifier = igGetIO_Nil()->ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
            if (can_copy && igShortcut_Nil(modifier | ImGuiKey_C, ImGuiInputFlags_RouteGlobal)) action = RIBBON_COPY_OBJECT;
            if (can_paste && igShortcut_Nil(modifier | ImGuiKey_V, ImGuiInputFlags_RouteGlobal)) action = RIBBON_PASTE_OBJECT;
        }
        igTextUnformatted("MARCUS", NULL); igSameLine(100, -1);
        igText("%s%s", name, modified ? "  (unsaved)" : "");
        igSameLine((float)width - 448, -1);
        igBeginDisabled(!loaded);
        if (igButton("Save copy", dialog_vec(104, 24))) action = RIBBON_SAVE_COPY;
        igEndDisabled(); igSameLine(0, 8);
        if (igButton("New movie", dialog_vec(104, 24))) action = RIBBON_NEW_MOVIE;
        igSameLine(0, 8); if (igButton("Open movie", dialog_vec(104, 24))) action = RIBBON_OPEN;
        igSameLine(0, 8); if (igButton("Settings", dialog_vec(96, 24))) action = RIBBON_SETTINGS;
        igSeparator();
        requested = *tab; external = requested != displayed_tab;
        if (igBeginTabBar("Editor modes", 0)) {
            for (index = 0; index < 4; ++index) {
                if (igBeginTabItem(tabs[index], NULL, external && requested == index ? ImGuiTabItemFlags_SetSelected : 0)) {
                    *tab = index; igEndTabItem();
                }
            }
            igEndTabBar();
        }
        displayed_tab = *tab;
        tools = tools_for(*tab, &count);
        for (index = 0; index < count; ++index) {
            if (index) igSameLine(0, 8);
            enabled = (loaded && (tools[index].action != RIBBON_CAMERAS || cameras > 0) &&
                (tools[index].action != RIBBON_EDIT_TEXT || selected_text) &&
                (tools[index].action != RIBBON_COPY_OBJECT || can_copy) &&
                (tools[index].action != RIBBON_PASTE_OBJECT || can_paste)) ||
                tools[index].action == RIBBON_NEW_SCENE || tools[index].action == RIBBON_STORYBOARD;
            active = (tools[index].action == RIBBON_MUTE && muted) ||
                (tools[index].action == RIBBON_CAMERA_GUIDES && show_cameras);
            label = tools[index].action == RIBBON_CAMERA_GUIDES ? (show_cameras ? "All cameras" : "Active only") : tools[index].label;
            if (active) igPushStyleColor_Vec4(ImGuiCol_Button, igGetStyle()->Colors[ImGuiCol_ButtonActive]);
            igBeginDisabled(!enabled);
            if (igButton(label, dialog_vec(104, 36))) action = tools[index].action;
            igEndDisabled(); if (active) igPopStyleColor(1);
            if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) igSetTooltip("%s", tools[index].hint);
        }
    }
    ui_panel_end(); return action;
}
