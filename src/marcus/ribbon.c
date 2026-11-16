#include "ribbon.h"

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
    {RIBBON_NEW_ACTOR, 11, "New actor", "Choose an actor to add at the current frame", 0xe9bc75},
    {RIBBON_NEW_PROP, 10, "New prop", "Choose a prop to add at the current frame", 0x78cbc4},
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
static float tool_step(int width, int count)
{
    float available;
    available = ((float)width - 280) / count;
    return available < 104 ? available : 104;
}
static void icon(MarcusUI *ui, float x, float y, int kind, unsigned long colour)
{
    int row;
    if (kind == 0)
    {
        ui_fill(ui, x, y + 2, 34, 24, 0x182126);
        ui_line(ui, x, y + 2, x + 34, y + 2, colour);
        ui_line(ui, x, y + 2, x, y + 26, colour);
        ui_line(ui, x, y + 26, x + 34, y + 26, colour);
        ui_line(ui, x + 34, y + 2, x + 34, y + 26, colour);
        ui_line(ui, x + 4, y + 21, x + 13, y + 10, colour);
        ui_line(ui, x + 13, y + 10, x + 21, y + 20, colour);
        ui_line(ui, x + 20, y + 20, x + 26, y + 14, colour);
        ui_fill(ui, x + 28, y + 15, 4, 18, colour);
        ui_fill(ui, x + 21, y + 22, 18, 4, colour);
    }
    else if (kind == 1)
    {
        ui_fill(ui, x + 1, y + 3, 34, 26, colour);
        for (row = 0; row < 4; ++row)
        {
            ui_fill(ui, x + 3 + row * 8, y + 5, 4, 3, 0x20262d);
            ui_fill(ui, x + 3 + row * 8, y + 24, 4, 3, 0x20262d);
        }
        ui_fill(ui, x + 5, y + 10, 11, 12, 0x20262d);
        ui_fill(ui, x + 20, y + 10, 11, 12, 0x20262d);
    }
    else if (kind == 2 || kind == 3)
    {
        for (row = 0; row < 12; ++row)
            ui_line(ui, x + (kind == 2 ? 8 + row : 28 - row), y + 16 - row,
                    x + (kind == 2 ? 8 + row : 28 - row), y + 16 + row, colour);
    }
    else if (kind == 4 || kind == 11)
    {
        ui_fill(ui, x + 13, y + 2, 12, 11, colour);
        ui_fill(ui, x + 9, y + 16, 20, 8, colour);
        ui_fill(ui, x + 11, y + 24, 5, 8, colour);
        ui_fill(ui, x + 22, y + 24, 5, 8, colour);
        if (kind == 11)
        {
            ui_fill(ui, x + 30, y + 20, 4, 18, colour);
            ui_fill(ui, x + 23, y + 27, 18, 4, colour);
        }
    }
    else if (kind == 5 || kind == 10)
    {
        ui_line(ui, x + 3, y + 9, x + 18, y + 2, colour);
        ui_line(ui, x + 18, y + 2, x + 33, y + 9, colour);
        ui_line(ui, x + 3, y + 9, x + 18, y + 16, colour);
        ui_line(ui, x + 18, y + 16, x + 33, y + 9, colour);
        ui_line(ui, x + 3, y + 9, x + 3, y + 25, colour);
        ui_line(ui, x + 33, y + 9, x + 33, y + 25, colour);
        ui_line(ui, x + 3, y + 25, x + 18, y + 32, colour);
        ui_line(ui, x + 33, y + 25, x + 18, y + 32, colour);
        ui_line(ui, x + 18, y + 16, x + 18, y + 32, colour);
        if (kind == 10)
        {
            ui_fill(ui, x + 30, y + 20, 4, 18, colour);
            ui_fill(ui, x + 23, y + 27, 18, 4, colour);
        }
    }
    else if (kind == 6)
    {
        ui_fill(ui, x + 11, y + 3, 3, 25, colour);
        ui_fill(ui, x + 28, y + 1, 3, 24, colour);
        ui_line(ui, x + 14, y + 4, x + 30, y + 2, colour);
        ui_fill(ui, x + 4, y + 24, 10, 7, colour);
        ui_fill(ui, x + 21, y + 21, 10, 7, colour);
    }
    else if (kind == 7)
    {
        ui_line(ui, x + 6, y + 7, x + 31, y + 7, colour);
        ui_line(ui, x + 6, y + 16, x + 31, y + 16, colour);
        ui_line(ui, x + 6, y + 25, x + 31, y + 25, colour);
        ui_fill(ui, x + 11, y + 3, 4, 9, colour);
        ui_fill(ui, x + 23, y + 12, 4, 9, colour);
        ui_fill(ui, x + 16, y + 21, 4, 9, colour);
    }
    else if (kind == 9)
    {
        ui_fill(ui, x + 5, y + 1, 10, 8, colour);
        ui_fill(ui, x + 20, y + 1, 10, 8, colour);
        ui_fill(ui, x + 5, y + 11, 24, 16, colour);
        ui_fill(ui, x + 8, y + 14, 18, 10, 0x25282d);
        ui_line(ui, x + 29, y + 15, x + 36, y + 11, colour);
        ui_line(ui, x + 36, y + 11, x + 36, y + 27, colour);
        ui_line(ui, x + 36, y + 27, x + 29, y + 23, colour);
        ui_line(ui, x + 17, y + 27, x + 11, y + 33, colour);
        ui_line(ui, x + 17, y + 27, x + 23, y + 33, colour);
    }
    else
    {
        ui_fill(ui, x + 2, y + 2, 32, 30, colour);
        ui_fill(ui, x + 4, y + 4, 28, 26, 0x25282d);
        ui_heading(ui, x + 8, y + 10, "Aa", colour);
        if (kind == 12)
        {
            ui_fill(ui, x + 30, y + 20, 4, 18, colour);
            ui_fill(ui, x + 23, y + 27, 18, 4, colour);
        }
    }
}
void ribbon_draw(MarcusUI *ui, int width, int tab, const char *name, int loaded, int modified, int muted,
                 unsigned long cameras, int show_cameras, int selected_text)
{
    static const char *tabs[] = {"SCENES", "ACTORS & PROPS", "SOUNDS", "WORDS"};
    static const float tab_x[] = {16, 148, 336, 468}, tab_w[] = {124, 180, 124, 124};
    const RibbonTool *tools;
    int index, count, enabled, hovered, active;
    float x, step;
    unsigned long colour;
    ui_fill(ui, 0, 0, (float)width, 36, 0x202328);
    ui_fill(ui, 16, 10, 4, 17, 0x78cbc4);
    ui_heading(ui, 29, 12, "MARCUS", 0xdde0e5);
    ui_label(ui, 128, 11, (float)width - 634, name, 0xdde0e5);
    if (modified) ui_text(ui, (float)width - 540, 12, "UNSAVED", 0xe9bc75);
    ui_button_sized(ui, (float)width - 448, 5, 104, 26, "Save copy", 0, loaded);
    ui_button_sized(ui, (float)width - 336, 5, 104, 26, "New movie", 0, 1);
    ui_button_sized(ui, (float)width - 224, 5, 104, 26, "Open movie", 0, 1);
    ui_button_sized(ui, (float)width - 112, 5, 96, 26, "Settings", 0, 1);
    ui_fill(ui, 0, 36, (float)width, 108, 0x292e35);
    for (index = 0; index < 4; ++index)
    {
        hovered = ui_inside(ui->mouse_x, ui->mouse_y, tab_x[index], 40, tab_w[index], 30);
        active = index == tab;
        ui_fill(ui, tab_x[index], 40, tab_w[index], 30, active ? 0x3c4853 : hovered ? 0x343c45 : 0x252a30);
        ui_heading_label(ui, tab_x[index] + 12, 49, tab_w[index] - 24, tabs[index], active ? 0xdde0e5 : 0x969da8);
        if (active) ui_fill(ui, tab_x[index], 68, tab_w[index], 3, 0x78cbc4);
    }
    ui_line(ui, 16, 72, (float)width - 16, 72, 0x424952);
    tools = tools_for(tab, &count);
    step = tool_step(width, count);
    for (index = 0; index < count; ++index)
    {
        x = 16 + index * step;
        enabled = (loaded && (tools[index].action != RIBBON_CAMERAS || cameras > 0) &&
                   (tools[index].action != RIBBON_EDIT_TEXT || selected_text)) || tools[index].action == RIBBON_NEW_SCENE || tools[index].action == RIBBON_STORYBOARD;
        hovered = enabled && ui_inside(ui->mouse_x, ui->mouse_y, x, 78, step - 8, 58);
        active = (tools[index].action == RIBBON_MUTE && muted) ||
                 (tools[index].action == RIBBON_CAMERA_GUIDES && show_cameras);
        if (hovered || active) ui_fill(ui, x, 78, step - 8, 58, active ? 0x375e64 : 0x3d4650);
        colour = enabled ? tools[index].colour : 0x59616c;
        icon(ui, x + (step - 46) / 2, 79, tools[index].icon, colour);
        ui_label(ui, x + 4, 117, step - 12,
                 tools[index].action == RIBBON_CAMERA_GUIDES ? (show_cameras ? "All cameras" : "Active only") : tools[index].label,
                 enabled ? 0xdde0e5 : 0x737b86);
        if (hovered) ui->hint = tools[index].hint;
    }
    ui_line(ui, (float)width - 254, 82, (float)width - 254, 131, 0x424952);
    ui_button_sized(ui, (float)width - 238, 84, 104, 26, "Resources...", 0, 1);
    ui_button_sized(ui, (float)width - 126, 84, 110, 26, "Content...", 0, 1);
    ui_text(ui, (float)width - 238, 120, "Original 3DMM content", 0x969da8);
    ui_line(ui, 0, MARCUS_RIBBON_HEIGHT, (float)width, MARCUS_RIBBON_HEIGHT, 0x424952);
}
int ribbon_hit(float x, float y, int width, int *tab, int loaded, unsigned long cameras, int selected_text)
{
    static const float tab_x[] = {16, 148, 336, 468}, tab_w[] = {124, 180, 124, 124};
    const RibbonTool *tools;
    int index, count;
    float step;
    if (loaded && ui_inside(x, y, (float)width - 448, 5, 104, 26)) return RIBBON_SAVE_COPY;
    if (ui_inside(x, y, (float)width - 336, 5, 104, 26)) return RIBBON_NEW_MOVIE;
    if (ui_inside(x, y, (float)width - 224, 5, 104, 26)) return RIBBON_OPEN;
    if (ui_inside(x, y, (float)width - 112, 5, 96, 26)) return RIBBON_SETTINGS;
    if (ui_inside(x, y, (float)width - 238, 84, 104, 26)) return RIBBON_RESOURCES;
    if (ui_inside(x, y, (float)width - 126, 84, 110, 26)) return RIBBON_CONTENT;
    for (index = 0; index < 4; ++index)
        if (ui_inside(x, y, tab_x[index], 40, tab_w[index], 30)) { *tab = index; return RIBBON_NONE; }
    tools = tools_for(*tab, &count);
    step = tool_step(width, count);
    for (index = 0; index < count; ++index)
        if (((loaded && (tools[index].action != RIBBON_CAMERAS || cameras > 0) &&
              (tools[index].action != RIBBON_EDIT_TEXT || selected_text)) || tools[index].action == RIBBON_NEW_SCENE || tools[index].action == RIBBON_STORYBOARD) &&
            ui_inside(x, y, 16 + index * step, 78, step - 8, 58))
            return tools[index].action;
    return RIBBON_NONE;
}
