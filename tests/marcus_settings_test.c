#include "settings.h"
#include <stdio.h>
#include <string.h>

#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            fprintf(stderr, "line %d: %s: %s\n", __LINE__, #value, message);                                           \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(int argc, char **argv)
{
    MarcusShortcuts original, changed, loaded;
    char message[256], label[96];
    int i;
    const char *bad;
    if (argc != 2)
        return 2;
    message[0] = 0;
    CHECK(SDL_Init(0));
    shortcuts_defaults(&original);
    CHECK(shortcuts_match(&original, SDLK_F, SDL_KMOD_NONE) == MARCUS_NEXT_FRAME);
    CHECK(shortcuts_match(&original, SDLK_D, SDL_KMOD_NONE) == MARCUS_PREV_FRAME);
    CHECK(shortcuts_match(&original, SDLK_G, SDL_KMOD_NONE) == MARCUS_NEXT_SCENE);
    CHECK(shortcuts_match(&original, SDLK_S, SDL_KMOD_NONE) == MARCUS_PREV_SCENE);
    CHECK(shortcuts_match(&original, SDLK_F, SDL_KMOD_SHIFT) == MARCUS_LAST_FRAME);
    CHECK(shortcuts_match(&original, SDLK_D, SDL_KMOD_SHIFT) == MARCUS_FIRST_FRAME);
    CHECK(shortcuts_match(&original, SDLK_G, SDL_KMOD_SHIFT) == MARCUS_LAST_SCENE);
    CHECK(shortcuts_match(&original, SDLK_S, SDL_KMOD_SHIFT) == MARCUS_FIRST_SCENE);
    CHECK(shortcuts_match(&original, 'F', SDL_KMOD_CAPS) == MARCUS_NEXT_FRAME);
    CHECK(shortcuts_match(&original, SDLK_F, SDL_KMOD_CTRL) == -1);
    CHECK(shortcuts_match(&original, SDLK_R, SDL_KMOD_NONE) == MARCUS_RESET_VIEW);
    changed = original;
    CHECK(!shortcuts_assign(&changed, MARCUS_NEXT_FRAME, SDLK_D, 0, message, sizeof(message)));
    CHECK(strstr(message, "Previous frame") && !memcmp(&original, &changed, sizeof(original)));
    CHECK(!shortcuts_assign(&changed, MARCUS_NEXT_FRAME, SDLK_ESCAPE, 0, message, sizeof(message)));
    CHECK(!shortcuts_assign(&changed, MARCUS_NEXT_FRAME, SDLK_COMMA, MARCUS_MOD_CTRL, message, sizeof(message)));
    CHECK(!shortcuts_assign(&changed, MARCUS_NEXT_FRAME, SDLK_LSHIFT, 0, message, sizeof(message)));
    CHECK(shortcuts_assign(&changed, MARCUS_NEXT_FRAME, SDLK_H, MARCUS_MOD_CTRL | MARCUS_MOD_SHIFT, message,
                           sizeof(message)));
    CHECK(shortcuts_match(&changed, SDLK_H, SDL_KMOD_CTRL | SDL_KMOD_SHIFT) == MARCUS_NEXT_FRAME);
    CHECK(shortcuts_match(&changed, SDLK_F, SDL_KMOD_NONE) == -1);
    shortcuts_label(&changed.binding[MARCUS_NEXT_FRAME], label, sizeof(label));
    CHECK(!strcmp(label, "Ctrl+Shift+H"));
    CHECK(shortcuts_save(argv[1], &changed, message, sizeof(message)));
    shortcuts_defaults(&loaded);
    CHECK(shortcuts_load(argv[1], &loaded, message, sizeof(message)));
    for (i = 0; i < MARCUS_ACTION_COUNT; ++i)
        CHECK(loaded.binding[i].key == changed.binding[i].key &&
              loaded.binding[i].modifiers == changed.binding[i].modifiers);
    CHECK(shortcuts_assign(&changed, MARCUS_NEXT_FRAME, SDLK_UNKNOWN, 0, message, sizeof(message)));
    CHECK(shortcuts_save(argv[1], &changed, message, sizeof(message)));
    CHECK(shortcuts_load(argv[1], &loaded, message, sizeof(message)) && !loaded.binding[MARCUS_NEXT_FRAME].key);
    bad = "marcus-shortcuts 1\nnext_frame 100 0\n";
    CHECK(SDL_SaveFile(argv[1], bad, strlen(bad)));
    CHECK(!shortcuts_load(argv[1], &loaded, message, sizeof(message)));
    CHECK(!loaded.binding[MARCUS_NEXT_FRAME].key);
    CHECK(SDL_SaveFile(argv[1], "broken settings", 15));
    CHECK(!shortcuts_load(argv[1], &loaded, message, sizeof(message)));
    CHECK(shortcuts_save(argv[1], &original, message, sizeof(message)));
    CHECK(shortcuts_load(argv[1], &loaded, message, sizeof(message)));
    CHECK(shortcuts_match(&loaded, SDLK_F, SDL_KMOD_NONE) == MARCUS_NEXT_FRAME);
    SDL_Quit();
    puts("Marcus shortcuts: defaults, modifiers, rebinding, conflicts, persistence, replacement and invalid files "
         "passed.");
    return 0;
}
