#ifndef MARCUS_CAMERA_CONTROLS_H
#define MARCUS_CAMERA_CONTROLS_H
#include "player.h"
#include <SDL3/SDL_scancode.h>
#ifdef __cplusplus
extern "C" {
#endif
enum MarcusFlyKey
{
    MARCUS_FLY_FORWARD = 1, MARCUS_FLY_BACK = 2, MARCUS_FLY_LEFT = 4, MARCUS_FLY_RIGHT = 8,
    MARCUS_FLY_UP = 16, MARCUS_FLY_DOWN = 32, MARCUS_FLY_FAST = 64, MARCUS_FLY_FAST_RIGHT = 128
};
int marcus_fly_key(SDL_Scancode code);
int marcus_fly_held_keys(void);
/* Shared navigation policy. The caller owns capture, held keys and elapsed time. */
int marcus_fly_step(MarcusCamera *camera, int keys, double seconds);
int marcus_fly_look(MarcusCamera *camera, float dx, float dy);
void marcus_fly_speed(MarcusCamera *camera, float wheel);
#ifdef __cplusplus
}
#endif
#endif
