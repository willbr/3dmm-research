#ifndef MARCUS_SETTINGS_H
#define MARCUS_SETTINGS_H
#include <SDL3/SDL.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MARCUS_MOD_SHIFT 1
#define MARCUS_MOD_CTRL 2
#define MARCUS_MOD_ALT 4
#define MARCUS_MOD_GUI 8

enum MarcusAction
{
    MARCUS_FIRST_FRAME,
    MARCUS_PREV_FRAME,
    MARCUS_NEXT_FRAME,
    MARCUS_LAST_FRAME,
    MARCUS_FIRST_SCENE,
    MARCUS_PREV_SCENE,
    MARCUS_NEXT_SCENE,
    MARCUS_LAST_SCENE,
    MARCUS_PLAY,
    MARCUS_VIEW,
    MARCUS_RESET_VIEW,
    MARCUS_OPEN,
    MARCUS_ACTION_COUNT
};
typedef struct MarcusShortcut
{
    SDL_Keycode key;
    unsigned int modifiers;
} MarcusShortcut;
typedef struct MarcusShortcuts
{
    MarcusShortcut binding[MARCUS_ACTION_COUNT];
    int pause_on_focus_loss;
    int renderer;
    int render_resolution;
    int work_renderer;
    int work_render_resolution;
} MarcusShortcuts;

#define MARCUS_RESOLUTION_COUNT 10
#define MARCUS_DEFAULT_RESOLUTION 2
typedef struct MarcusRenderResolution
{
    int width, height;
    const char *name;
} MarcusRenderResolution;
const MarcusRenderResolution *marcus_render_resolution(int index);

void shortcuts_defaults(MarcusShortcuts *shortcuts);
const char *shortcuts_action_name(int action);
unsigned int shortcuts_modifiers(SDL_Keymod modifiers);
int shortcuts_is_modifier(SDL_Keycode key);
int shortcuts_reserved(SDL_Keycode key, unsigned int modifiers);
void shortcuts_label(const MarcusShortcut *shortcut, char *text, size_t capacity);
int shortcuts_match(const MarcusShortcuts *shortcuts, SDL_Keycode key, SDL_Keymod modifiers);
int shortcuts_assign(MarcusShortcuts *shortcuts, int action, SDL_Keycode key, unsigned int modifiers, char *message,
                     size_t capacity);
/* Missing files are successful. Invalid files leave the caller's values intact.
 * Saves replace the file atomically via a sibling temporary file. */
int shortcuts_load(const char *path, MarcusShortcuts *shortcuts, char *message, size_t capacity);
int shortcuts_save(const char *path, const MarcusShortcuts *shortcuts, char *message, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
