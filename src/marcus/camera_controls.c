#include "camera_controls.h"
#include <SDL3/SDL.h>
#include <math.h>

static double clamp(double value, double low, double high)
{
    return value < low ? low : value > high ? high : value;
}
static const struct
{
    SDL_Scancode code;
    int movement;
} flight_bindings[] = {
    {SDL_SCANCODE_E, MARCUS_FLY_FORWARD}, {SDL_SCANCODE_D, MARCUS_FLY_BACK},
    {SDL_SCANCODE_S, MARCUS_FLY_LEFT}, {SDL_SCANCODE_F, MARCUS_FLY_RIGHT},
    {SDL_SCANCODE_R, MARCUS_FLY_UP}, {SDL_SCANCODE_W, MARCUS_FLY_DOWN},
    {SDL_SCANCODE_LSHIFT, MARCUS_FLY_FAST}, {SDL_SCANCODE_RSHIFT, MARCUS_FLY_FAST_RIGHT}
};
int marcus_fly_key(SDL_Scancode code)
{
    size_t i;
    for (i = 0; i < SDL_arraysize(flight_bindings); ++i)
        if (code == flight_bindings[i].code) return flight_bindings[i].movement;
    return 0;
}
int marcus_fly_held_keys(void)
{
    SDL_Keymod modifiers;
    size_t i;
    int keys;
    modifiers = SDL_GetModState();
    keys = 0;
    for (i = 0; i < SDL_arraysize(flight_bindings); ++i)
        if (SDL_GetKeyboardState(NULL)[flight_bindings[i].code]) keys |= flight_bindings[i].movement;
    if (modifiers & SDL_KMOD_LSHIFT) keys |= MARCUS_FLY_FAST;
    if (modifiers & SDL_KMOD_RSHIFT) keys |= MARCUS_FLY_FAST_RIGHT;
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
