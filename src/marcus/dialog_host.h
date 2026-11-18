#ifndef MARCUS_DIALOG_HOST_H
#define MARCUS_DIALOG_HOST_H
#include "ui.h"
/* File/codec/worker adapters. All dialog widgets and input live in C89. */
enum MarcusDialogKind { DIALOG_BACKGROUNDS, DIALOG_MODELS, DIALOG_SCENES, DIALOG_FRAMES, DIALOG_CAMERAS, DIALOG_MOVIES };
enum MarcusDialogAction {
    DIALOG_SELECT, DIALOG_ACCEPT, DIALOG_CANCEL, DIALOG_BACK, DIALOG_UP, DIALOG_GO,
    DIALOG_ADDRESS, DIALOG_SEARCH, DIALOG_FILENAME, DIALOG_FILTER, DIALOG_SORT,
    DIALOG_GRID, DIALOG_RECENT, DIALOG_MOVIE_FOLDER, DIALOG_HOME, DIALOG_FAVOURITE,
    DIALOG_FAVOURITE_GO, DIALOG_CALCULATE, DIALOG_SCOPE, DIALOG_ZOOM,
    DIALOG_EDIT_BOARD, DIALOG_NEW_ANGLE, DIALOG_DROP, DIALOG_REFRESH, DIALOG_FIT
};
typedef struct MarcusDialogState {
    int open, kind, count, selected, current, grid, filter, sort_date, recent, loading, opening, calculating;
    int whole_movie, fit, storyboard, favourite_count, is_favourite, can_back;
    float zoom, minimum_zoom, preview_width, preview_height;
    char title[96], confirm[48], status[2048], error[1024];
    char address[4096], search[512], filename[4096];
} MarcusDialogState;
typedef struct MarcusDialogItem {
    char label[1024], detail[2048];
    SDL_Texture *texture;
    int directory, scene, transition;
    long frame;
} MarcusDialogItem;
typedef struct MarcusDialogHost {
    void (*state)(void *, MarcusDialogState *);
    /* Negative first polls completions; otherwise prepare the visible range. */
    void (*prepare)(void *, SDL_Renderer *, int first, int last);
    int (*item)(void *, SDL_Renderer *, int index, MarcusDialogItem *);
    void (*action)(void *, int action, int index, float value, const char *text);
} MarcusDialogHost;
#ifdef __cplusplus
extern "C" {
#endif
extern const MarcusDialogHost marcus_movie_dialog_host;
extern const MarcusDialogHost marcus_background_dialog_host;
extern const MarcusDialogHost marcus_scene_dialog_host;
extern const MarcusDialogHost marcus_camera_dialog_host;
void dialog_retire_texture(SDL_Texture *texture);
SDL_Texture *dialog_preview_texture(SDL_Renderer *, const unsigned char *rgba, int width, int height, int pitch);
/* Release only after the shared ImGui draw data has been submitted. */
void dialog_release_textures(void);
#ifdef __cplusplus
}
#endif
#endif
