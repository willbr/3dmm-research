#include "camera_controls.h"
#include <SDL3/SDL.h>
#include <math.h>

static double clamp(double value, double low, double high)
{
    return value < low ? low : value > high ? high : value;
}
int marcus_fly_key(SDL_Scancode code)
{
    switch (code)
    {
    case SDL_SCANCODE_W: return MARCUS_FLY_FORWARD;
    case SDL_SCANCODE_S: return MARCUS_FLY_BACK;
    case SDL_SCANCODE_A: return MARCUS_FLY_LEFT;
    case SDL_SCANCODE_D: return MARCUS_FLY_RIGHT;
    case SDL_SCANCODE_E: return MARCUS_FLY_UP;
    case SDL_SCANCODE_Q: return MARCUS_FLY_DOWN;
    case SDL_SCANCODE_LSHIFT: return MARCUS_FLY_FAST;
    case SDL_SCANCODE_RSHIFT: return MARCUS_FLY_FAST_RIGHT;
    default: return 0;
    }
}
int marcus_fly_held_keys(void)
{
    int keys;
    keys = 0;
    if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_W]) keys |= MARCUS_FLY_FORWARD;
    if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_S]) keys |= MARCUS_FLY_BACK;
    if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_A]) keys |= MARCUS_FLY_LEFT;
    if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_D]) keys |= MARCUS_FLY_RIGHT;
    if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_E]) keys |= MARCUS_FLY_UP;
    if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_Q]) keys |= MARCUS_FLY_DOWN;
    if (SDL_GetModState() & SDL_KMOD_LSHIFT) keys |= MARCUS_FLY_FAST;
    if (SDL_GetModState() & SDL_KMOD_RSHIFT) keys |= MARCUS_FLY_FAST_RIGHT;
    return keys;
}
int marcus_fly_step(MarcusCamera *camera, int keys, double seconds)
{
    double yaw, pitch, forward, side, up, dx, dy, dz, length, distance;
    if (!camera || seconds <= 0 || seconds != seconds) return 0;
    forward = ((keys & MARCUS_FLY_FORWARD) != 0) - ((keys & MARCUS_FLY_BACK) != 0);
    side = ((keys & MARCUS_FLY_RIGHT) != 0) - ((keys & MARCUS_FLY_LEFT) != 0);
    up = ((keys & MARCUS_FLY_UP) != 0) - ((keys & MARCUS_FLY_DOWN) != 0);
    yaw = camera->yaw * 0.01745329252;
    pitch = camera->pitch * 0.01745329252;
    dx = -sin(yaw) * cos(pitch) * forward + cos(yaw) * side;
    dy = -sin(pitch) * forward + up;
    dz = -cos(yaw) * cos(pitch) * forward - sin(yaw) * side;
    length = sqrt(dx * dx + dy * dy + dz * dz);
    if (length == 0) return 0;
    if (seconds > 0.1) seconds = 0.1;
    distance = camera->move_speed * seconds * ((keys & (MARCUS_FLY_FAST | MARCUS_FLY_FAST_RIGHT)) ? 4 : 1) / length;
    camera->x = (float)clamp(camera->x + dx * distance, -32700, 32700);
    camera->y = (float)clamp(camera->y + dy * distance, -32700, 32700);
    camera->z = (float)clamp(camera->z + dz * distance, -32700, 32700);
    return 1;
}
int marcus_fly_look(MarcusCamera *camera, float dx, float dy)
{
    if (!camera || (dx == 0 && dy == 0)) return 0;
    camera->yaw = (float)fmod(camera->yaw - dx * 0.15, 360);
    camera->pitch = (float)clamp(camera->pitch + dy * 0.15, -90, 90);
    return 1;
}
void marcus_fly_speed(MarcusCamera *camera, float wheel)
{
    if (camera) camera->move_speed = (float)clamp(camera->move_speed * pow(1.25, wheel), 0.1, 10000);
}
