/* Headless navigation math regression checks; no application UI input. */
#include "camera_controls.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void check(int ok, const char *message)
{
    if (!ok) { fprintf(stderr, "%s\n", message); exit(1); }
}
static int near(double actual, double expected)
{
    return fabs(actual - expected) < 0.0001;
}
static MarcusCamera camera(void)
{
    MarcusCamera result = {0};
    result.move_speed = 10;
    return result;
}
static double distance(MarcusCamera c)
{
    return sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
}
int main(void)
{
    MarcusCamera a, b;
    int i;
    a = camera(); b = camera();
    for (i = 0; i < 60; ++i) marcus_fly_step(&a, MARCUS_FLY_FORWARD, 1.0 / 60);
    for (i = 0; i < 120; ++i) marcus_fly_step(&b, MARCUS_FLY_FORWARD, 1.0 / 120);
    check(near(a.z, -10) && near(a.z, b.z), "Movement depends on elapsed time, not frame rate or key repeat");
    a = camera();
    check(!marcus_fly_step(&a, MARCUS_FLY_FORWARD | MARCUS_FLY_BACK | MARCUS_FLY_LEFT | MARCUS_FLY_RIGHT |
        MARCUS_FLY_UP | MARCUS_FLY_DOWN, .1) && near(distance(a), 0), "Opposing keys cancel");
    marcus_fly_step(&a, MARCUS_FLY_FORWARD | MARCUS_FLY_RIGHT, .1);
    check(near(distance(a), 1), "Diagonal movement keeps the same speed");
    a = camera(); marcus_fly_step(&a, MARCUS_FLY_FORWARD | MARCUS_FLY_RIGHT | MARCUS_FLY_UP, .1);
    check(near(distance(a), 1), "Three-axis movement keeps the same speed");
    a = camera(); a.yaw = 90; a.pitch = 30;
    marcus_fly_step(&a, MARCUS_FLY_FORWARD, .1);
    check(near(a.x, -sqrt(.75)) && near(a.y, -.5) && near(a.z, 0), "Forward follows yaw and pitch");
    a = camera(); a.yaw = 90; a.pitch = 30;
    marcus_fly_step(&a, MARCUS_FLY_UP, .1);
    check(near(a.y, 1) && near(a.x, 0) && near(a.z, 0), "Q/E use world vertical");
    for (i = MARCUS_FLY_FAST; i <= (MARCUS_FLY_FAST | MARCUS_FLY_FAST_RIGHT); i += MARCUS_FLY_FAST)
    {
        a = camera(); marcus_fly_step(&a, MARCUS_FLY_FORWARD | i, .1);
        check(near(distance(a), 4), "Either or both Shift keys give four times speed");
    }
    a = camera(); marcus_fly_step(&a, MARCUS_FLY_FORWARD, 10);
    check(near(a.z, -1), "Long frames are capped at 100ms");
    a = camera(); a.x = 32700; a.y = 32700; a.z = -32700;
    marcus_fly_step(&a, MARCUS_FLY_FORWARD | MARCUS_FLY_RIGHT | MARCUS_FLY_UP, .1);
    check(near(a.x, 32700) && near(a.y, 32700) && near(a.z, -32700), "Movement respects world limits");
    a = camera();
    check(!marcus_fly_step(&a, MARCUS_FLY_FORWARD, 0) && !marcus_fly_step(&a, MARCUS_FLY_FORWARD, -.1), "No movement without positive elapsed time");
    check(!marcus_fly_look(&a, 0, 0), "Stationary mouse leaves the camera unchanged");
    marcus_fly_look(&a, 100, -100);
    check(near(a.yaw, -15) && near(a.pitch, -15), "Both cameras use 0.15-degree mouse sensitivity");
    a.yaw = 359; marcus_fly_look(&a, -100, 10000);
    check(near(a.yaw, 14) && near(a.pitch, 90), "Yaw wraps and downward pitch reaches 90 degrees");
    marcus_fly_look(&a, 0, -10000);
    check(near(a.pitch, -90), "Upward pitch reaches minus 90 degrees");
    a = camera(); marcus_fly_speed(&a, 2);
    check(near(a.move_speed, 15.625) && near(distance(a), 0), "Wheel adjusts speed without moving the camera");
    marcus_fly_speed(&a, -2);
    check(near(a.move_speed, 10), "Reverse wheel restores speed");
    marcus_fly_speed(&a, 1000); check(near(a.move_speed, 10000), "Maximum flight speed");
    marcus_fly_speed(&a, -1000); check(near(a.move_speed, .1), "Minimum flight speed");
    check(marcus_fly_key(SDL_SCANCODE_W) == MARCUS_FLY_FORWARD && marcus_fly_key(SDL_SCANCODE_S) == MARCUS_FLY_BACK &&
        marcus_fly_key(SDL_SCANCODE_A) == MARCUS_FLY_LEFT && marcus_fly_key(SDL_SCANCODE_D) == MARCUS_FLY_RIGHT &&
        marcus_fly_key(SDL_SCANCODE_Q) == MARCUS_FLY_DOWN && marcus_fly_key(SDL_SCANCODE_E) == MARCUS_FLY_UP &&
        marcus_fly_key(SDL_SCANCODE_LSHIFT) == MARCUS_FLY_FAST && marcus_fly_key(SDL_SCANCODE_RSHIFT) == MARCUS_FLY_FAST_RIGHT &&
        marcus_fly_key(SDL_SCANCODE_SPACE) == 0, "Shared physical key mapping");
    puts("Shared Fly movement, mouse look and speed checks passed.");
    return 0;
}
