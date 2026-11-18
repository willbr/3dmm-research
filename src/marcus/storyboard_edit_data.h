#ifndef MARCUS_STORYBOARD_EDIT_DATA_H
#define MARCUS_STORYBOARD_EDIT_DATA_H
#include "player.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    MARCUS_BOARD_WIDTH = 544, MARCUS_BOARD_HEIGHT = 306,
    MARCUS_BOARD_PIXELS = MARCUS_BOARD_WIDTH * MARCUS_BOARD_HEIGHT,
    MARCUS_BOARD_ANGLE_LIMIT = 64, MARCUS_BOARD_LIGHT_LIMIT = 16,
    MARCUS_BOARD_COLOUR_LIMIT = 256, MARCUS_BOARD_SWATCH_COUNT = 10
};
typedef struct MarcusBoardPose
{
    MarcusCamera camera;
    float roll, fov, near_clip, far_clip;
} MarcusBoardPose;
typedef struct MarcusBoardLightValues
{
    float x, y, z, yaw, pitch, intensity;
    int point;
} MarcusBoardLightValues;
typedef struct MarcusBoardEditValues
{
    int angle_count, light_count, colour_count;
    MarcusBoardPose angles[MARCUS_BOARD_ANGLE_LIMIT];
    MarcusBoardLightValues lights[MARCUS_BOARD_LIGHT_LIMIT];
    unsigned colours[MARCUS_BOARD_COLOUR_LIMIT];
} MarcusBoardEditValues;

extern const unsigned marcus_board_swatches[MARCUS_BOARD_SWATCH_COUNT];
void marcus_board_projection(const MarcusBoardPose *, MarcusProjection *);
int marcus_board_pose_from_view(const MarcusImage *work, MarcusBoardPose *pose);
void marcus_board_light_matrix(const MarcusBoardLightValues *, float matrix[16]);
int marcus_board_light_from_matrix(const MarcusBoardLightValues *, const float matrix[16], int rotate,
                                   MarcusBoardLightValues *);
void marcus_board_stroke(unsigned char *pixels, int x0, int y0, int x1, int y1, int size, unsigned char colour);
/* Existing colour, next unused slot, or -1 when all slots are in use. Each
 * surface borrows MARCUS_BOARD_PIXELS bytes of logical ink indices. */
int marcus_board_colour_slot(const unsigned *colours, int count, unsigned rgb,
                             const unsigned char *const *surfaces, int angle_count);

#ifdef __cplusplus
}
#endif
#endif
