#include "settings.h"
#include "player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *action_names[MARCUS_ACTION_COUNT] = {"First frame",  "Previous frame",  "Next frame", "Last frame",
                                                        "First scene",  "Previous scene",  "Next scene", "Last scene",
                                                        "Play / pause", "Cycle view mode", "Reset view", "Open movie"};
static const char *action_ids[MARCUS_ACTION_COUNT] = {"first_frame", "prev_frame", "next_frame", "last_frame",
                                                      "first_scene", "prev_scene", "next_scene", "last_scene",
                                                      "play",        "view",       "reset_view", "open"};
static SDL_Keycode normalize_key(SDL_Keycode key)
{
    return key >= 'A' && key <= 'Z' ? key + ('a' - 'A') : key;
}
void shortcuts_defaults(MarcusShortcuts *shortcuts)
{
    static const SDL_Keycode keys[MARCUS_ACTION_COUNT] = {SDLK_D, SDLK_D, SDLK_F,     SDLK_F, SDLK_S, SDLK_S,
                                                          SDLK_G, SDLK_G, SDLK_SPACE, SDLK_C, SDLK_R, SDLK_O};
    int i;
    memset(shortcuts, 0, sizeof(*shortcuts));
    for (i = 0; i < MARCUS_ACTION_COUNT; ++i)
        shortcuts->binding[i].key = keys[i];
    shortcuts->binding[MARCUS_FIRST_FRAME].modifiers = MARCUS_MOD_SHIFT;
    shortcuts->binding[MARCUS_LAST_FRAME].modifiers = MARCUS_MOD_SHIFT;
    shortcuts->binding[MARCUS_FIRST_SCENE].modifiers = MARCUS_MOD_SHIFT;
    shortcuts->binding[MARCUS_LAST_SCENE].modifiers = MARCUS_MOD_SHIFT;
#ifdef __APPLE__
    shortcuts->binding[MARCUS_OPEN].modifiers = MARCUS_MOD_GUI;
#else
    shortcuts->binding[MARCUS_OPEN].modifiers = MARCUS_MOD_CTRL;
#endif
}
const char *shortcuts_action_name(int action)
{
    return action >= 0 && action < MARCUS_ACTION_COUNT ? action_names[action] : "Unknown action";
}
unsigned int shortcuts_modifiers(SDL_Keymod modifiers)
{
    unsigned int result;
    result = 0;
    if (modifiers & SDL_KMOD_SHIFT)
        result |= MARCUS_MOD_SHIFT;
    if (modifiers & SDL_KMOD_CTRL)
        result |= MARCUS_MOD_CTRL;
    if (modifiers & SDL_KMOD_ALT)
        result |= MARCUS_MOD_ALT;
    if (modifiers & SDL_KMOD_GUI)
        result |= MARCUS_MOD_GUI;
    return result;
}
int shortcuts_is_modifier(SDL_Keycode key)
{
    return key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LCTRL || key == SDLK_RCTRL || key == SDLK_LALT ||
           key == SDLK_RALT || key == SDLK_LGUI || key == SDLK_RGUI;
}
int shortcuts_reserved(SDL_Keycode key, unsigned int modifiers)
{
    return key == SDLK_ESCAPE || (key == SDLK_COMMA && (modifiers & (MARCUS_MOD_CTRL | MARCUS_MOD_GUI))) ||
           ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && modifiers == MARCUS_MOD_ALT);
}
void shortcuts_label(const MarcusShortcut *shortcut, char *text, size_t capacity)
{
    if (!shortcut->key)
    {
        SDL_strlcpy(text, "Unbound", capacity);
        return;
    }
    SDL_snprintf(text, capacity, "%s%s%s%s%s", (shortcut->modifiers & MARCUS_MOD_CTRL) ? "Ctrl+" : "",
                 (shortcut->modifiers & MARCUS_MOD_ALT) ? "Alt+" : "",
                 (shortcut->modifiers & MARCUS_MOD_SHIFT) ? "Shift+" : "",
                 (shortcut->modifiers & MARCUS_MOD_GUI) ? "Cmd+" : "", SDL_GetKeyName(shortcut->key));
}
int shortcuts_match(const MarcusShortcuts *shortcuts, SDL_Keycode key, SDL_Keymod modifiers)
{
    int i;
    unsigned int mask;
    mask = shortcuts_modifiers(modifiers);
    key = normalize_key(key);
    for (i = 0; i < MARCUS_ACTION_COUNT; ++i)
        if (key && shortcuts->binding[i].key == key && shortcuts->binding[i].modifiers == mask)
            return i;
    return -1;
}
int shortcuts_assign(MarcusShortcuts *shortcuts, int action, SDL_Keycode key, unsigned int modifiers, char *message,
                     size_t capacity)
{
    int i;
    char label[96];
    MarcusShortcut binding;
    if (action < 0 || action >= MARCUS_ACTION_COUNT || modifiers > 15 || shortcuts_is_modifier(key) ||
        shortcuts_reserved(key, modifiers))
    {
        SDL_strlcpy(message, "That key combination is reserved. Choose another key.", capacity);
        return 0;
    }
    binding.key = normalize_key(key);
    binding.modifiers = key ? modifiers : 0;
    for (i = 0; i < MARCUS_ACTION_COUNT; ++i)
        if (i != action && binding.key && binding.key == shortcuts->binding[i].key &&
            binding.modifiers == shortcuts->binding[i].modifiers)
        {
            shortcuts_label(&binding, label, sizeof(label));
            SDL_snprintf(message, capacity, "%s is assigned to %s. Choose another key.", label, action_names[i]);
            return 0;
        }
    shortcuts->binding[action] = binding;
    if (capacity)
        message[0] = 0;
    return 1;
}
int shortcuts_load(const char *path, MarcusShortcuts *shortcuts, char *message, size_t capacity)
{
    MarcusShortcuts loaded;
    SDL_PathInfo info;
    char *data, *line, *cursor, id[64], extra;
    unsigned long key;
    unsigned int modifiers, seen;
    int i, j, valid, pause_seen, renderer_seen;
    char renderer_id[64];
    if (!path[0] || !SDL_GetPathInfo(path, &info))
        return 1;
    if (info.type != SDL_PATHTYPE_FILE || info.size > 8192)
    {
        SDL_strlcpy(message, "The settings file is invalid. Default settings are in use.", capacity);
        return 0;
    }
    data = (char *)SDL_LoadFile(path, NULL);
    if (!data)
    {
        SDL_strlcpy(message, "Could not read saved settings. Default settings are in use.", capacity);
        return 0;
    }
    loaded = *shortcuts;
    seen = 0;
    pause_seen = 0;
    renderer_seen = 0;
    line = SDL_strtok_r(data, "\r\n", &cursor);
    valid = line && !strcmp(line, "marcus-shortcuts 1");
    while (valid && (line = SDL_strtok_r(NULL, "\r\n", &cursor)) != NULL)
    {
        if (SDL_sscanf(line, "%63s", id) == 1 && !strcmp(id, "renderer"))
        {
            if (renderer_seen || SDL_sscanf(line, "%63s %63s %c", id, renderer_id, &extra) != 2 ||
                (loaded.renderer = marcus_renderer_from_id(renderer_id)) < 0)
            {
                valid = 0;
                break;
            }
            renderer_seen = 1;
            continue;
        }
        if (SDL_sscanf(line, "%63s", id) == 1 && !strcmp(id, "pause_on_focus_loss"))
        {
            if (pause_seen || SDL_sscanf(line, "%63s %lu %c", id, &key, &extra) != 2 || key > 1)
            {
                valid = 0;
                break;
            }
            loaded.pause_on_focus_loss = (int)key;
            pause_seen = 1;
            continue;
        }
        if (SDL_sscanf(line, "%63s %lu %u %c", id, &key, &modifiers, &extra) != 3 || key > 0xffffffffUL ||
            modifiers > 15)
        {
            valid = 0;
            break;
        }
        for (i = 0; i < MARCUS_ACTION_COUNT && strcmp(id, action_ids[i]); ++i)
        {
        }
        if (i == MARCUS_ACTION_COUNT || (seen & (1u << i)) || shortcuts_is_modifier((SDL_Keycode)key) ||
            shortcuts_reserved((SDL_Keycode)key, modifiers))
        {
            valid = 0;
            break;
        }
        seen |= 1u << i;
        loaded.binding[i].key = normalize_key((SDL_Keycode)key);
        loaded.binding[i].modifiers = key ? modifiers : 0;
    }
    for (i = 0; valid && i < MARCUS_ACTION_COUNT; ++i)
        for (j = i + 1; j < MARCUS_ACTION_COUNT; ++j)
            if (loaded.binding[i].key && loaded.binding[i].key == loaded.binding[j].key &&
                loaded.binding[i].modifiers == loaded.binding[j].modifiers)
                valid = 0;
    SDL_free(data);
    if (!valid)
    {
        SDL_strlcpy(message, "The settings file is invalid. Default settings are in use.", capacity);
        return 0;
    }
    *shortcuts = loaded;
    if (capacity)
        message[0] = 0;
    return 1;
}
int shortcuts_save(const char *path, const MarcusShortcuts *shortcuts, char *message, size_t capacity)
{
    char data[2048], temporary[1100];
    size_t length;
    int i;
    if (!path[0] || SDL_snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary))
    {
        SDL_strlcpy(message, "The settings location is unavailable.", capacity);
        return 0;
    }
    SDL_snprintf(data, sizeof(data), "marcus-shortcuts 1\npause_on_focus_loss %d\nrenderer %s\n",
                 shortcuts->pause_on_focus_loss != 0, marcus_renderer_id(shortcuts->renderer));
    length = strlen(data);
    for (i = 0; i < MARCUS_ACTION_COUNT; ++i)
        length += SDL_snprintf(data + length, sizeof(data) - length, "%s %lu %u\n", action_ids[i],
                               (unsigned long)shortcuts->binding[i].key, shortcuts->binding[i].modifiers);
    if (!SDL_SaveFile(temporary, data, length) || !SDL_RenamePath(temporary, path))
    {
        SDL_strlcpy(message, "Could not save settings. Check that your settings folder is writable.", capacity);
        SDL_RemovePath(temporary);
        return 0;
    }
    if (capacity)
        message[0] = 0;
    return 1;
}
