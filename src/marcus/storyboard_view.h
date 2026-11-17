#ifndef MARCUS_STORYBOARD_VIEW_H
#define MARCUS_STORYBOARD_VIEW_H
#include "storyboard_editor.h"
#include "camera_guide.h"
/* Copied values and borrowed preview geometry; no C++ or widget types. */
typedef struct MarcusBoardField {
    char name[32];
    float value, low, high, step;
} MarcusBoardField;
typedef struct MarcusBoardLightOverlay {
    MarcusCameraGuide shape, direction;
    float x, y;
    int visible, selected;
    char label[64];
} MarcusBoardLightOverlay;
typedef struct MarcusBoardEditorState {
    int open, tab, angle, angles, light, lights, point, grid, has_work, inspecting;
    int picking, brush, can_undo, can_bake, field_count, editing;
    unsigned long rgb, swatches[10], dither[64];
    float colour[3], okhsl[3];
    SDL_FRect canvas, tools, bar;
    SDL_Texture *texture;
    const SDL_Vertex *vertices;
    const int *indices;
    int index_count;
    MarcusBoardField fields[9];
    MarcusCameraGuide camera_guide;
    MarcusBoardLightOverlay overlays[16];
    char error[512];
} MarcusBoardEditorState;
enum MarcusBoardCommand {
    BOARD_DONE, BOARD_TAB, BOARD_ANGLE, BOARD_NEW_ANGLE, BOARD_USE_WORK, BOARD_GRID,
    BOARD_RGB, BOARD_HSL, BOARD_SWATCH, BOARD_PICK, BOARD_BRUSH, BOARD_BAKE, BOARD_CLEAR,
    BOARD_UNDO, BOARD_LIGHT, BOARD_LIGHT_ADD, BOARD_LIGHT_REMOVE, BOARD_POINT,
    BOARD_FRAME_LIGHTS, BOARD_SHOT_VIEW, BOARD_FIELD_BEGIN, BOARD_FIELD_SET,
    BOARD_FIELD_END, BOARD_FIELD_CANCEL
};
#ifdef __cplusplus
extern "C" {
#endif
void storyboard_editor_prepare(MarcusStoryboardEditor *, SDL_Renderer *);
void storyboard_editor_state(MarcusStoryboardEditor *, MarcusBoardEditorState *);
void storyboard_editor_command(MarcusStoryboardEditor *, int command, int index, float value, const float *colour);
#ifdef __cplusplus
}
#endif
#endif
