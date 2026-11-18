#ifndef MARCUS_STORYBOARD_EDITOR_BACKEND_H
#define MARCUS_STORYBOARD_EDITOR_BACKEND_H
#include "storyboard_edit_data.h"
#include "camera_guide.h"
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusBoardEditorBackend MarcusBoardEditorBackend;
MarcusBoardEditorBackend *marcus_board_editor_backend_create(void);
void marcus_board_editor_backend_destroy(MarcusBoardEditorBackend *);
int marcus_board_editor_new_scene(MarcusPlayer *, const char *resources, const char *content);
int marcus_board_editor_load(MarcusBoardEditorBackend *, MarcusPlayer *, MarcusBoardEditValues *,
                            unsigned swatches[MARCUS_BOARD_SWATCH_COUNT]);
/* A logical ink surface is borrowed until the next load or destruction. */
unsigned char *marcus_board_editor_pixels(MarcusBoardEditorBackend *, int angle);
int marcus_board_editor_append_surface(MarcusBoardEditorBackend *);
void marcus_board_editor_default_light(MarcusBoardLightValues *);
/* Stage a validated snapshot for comparison. Accept it only after the C
 * controller successfully applies the draft or selects its existing angle. */
int marcus_board_editor_stage(MarcusBoardEditorBackend *, const MarcusBoardEditValues *, int *changed);
int marcus_board_editor_apply(MarcusBoardEditorBackend *, MarcusPlayer *, int angle, const char *resources, const char *content);
void marcus_board_editor_accept(MarcusBoardEditorBackend *);
int marcus_board_editor_bake(MarcusBoardEditorBackend *, int angle, const MarcusBoardPose *, unsigned char colour);
int marcus_board_editor_can_bake(const MarcusBoardPose *);
unsigned marcus_board_editor_okhsl(const float colour[3]);
void marcus_board_editor_to_okhsl(unsigned rgb, float colour[3]);
void marcus_board_editor_dither(const MarcusBoardEditorBackend *, unsigned rgb, unsigned long pixels[64]);
void marcus_board_editor_light_widget(const MarcusProjection *, const MarcusBoardLightValues *, float height,
                                      MarcusCameraGuide *shape, MarcusCameraGuide *direction, float centre[3], int *visible);
/* Returned pixels are borrowed until the next player render or draft render.
 * Grid geometry is borrowed until rebuilding the grid or destroying the draft. */
int marcus_board_editor_render(MarcusBoardEditorBackend *, MarcusPlayer *, const MarcusBoardEditValues *, int angle,
                               const MarcusBoardPose *inspection, const unsigned char **rgba);
int marcus_board_editor_build_grid(MarcusBoardEditorBackend *, const MarcusBoardPose *, const SDL_FRect *);
void marcus_board_editor_grid(const MarcusBoardEditorBackend *, const SDL_Vertex **, const int **indices, int *count);

#ifdef __cplusplus
}
#endif
#endif
