#include "storyboard_edit_data.h"
#include <SDL3/SDL.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

const unsigned marcus_board_swatches[MARCUS_BOARD_SWATCH_COUNT] = {
    0xd3d4d6, 0x000000, 0xffffff, 0x9ebfd9, 0xdea5a2, 0xe6d8a0, 0xaac9ad, 0x505050, 0x949494, 0xc4c4c4
};
static const float radians = .01745329252f;

static float clamp(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static float wrap_yaw(float yaw)
{
    /* Match remainder's nearest-even choice at half turns. */
    float turn = SDL_fmodf(yaw, 720.f);
    if (turn >= 540.f) return turn - 720.f;
    if (turn <= -540.f) return turn + 720.f;
    if (turn == -360.f) return -0.f;
    if (turn > 180.f) return turn - 360.f;
    if (turn < -180.f) return turn + 360.f;
    return turn;
}

int marcus_board_pose_from_view(const MarcusImage *work, MarcusBoardPose *pose)
{
    const float *right;
    float yaw, pitch, cosine, sine;
    if (!work || !work->projection.valid || work->projection.orthographic) return 0;
    right = work->projection.transform;
    pose->camera = work->camera;
    pose->camera.yaw = wrap_yaw(pose->camera.yaw);
    pose->camera.pitch = clamp(pose->camera.pitch, -90.f, 90.f);
    yaw = pose->camera.yaw * radians;
    pitch = pose->camera.pitch * radians;
    cosine = right[0] * SDL_cosf(yaw) - right[2] * SDL_sinf(yaw);
    sine = -right[0] * SDL_sinf(yaw) * SDL_sinf(pitch) + right[1] * SDL_cosf(pitch) -
           right[2] * SDL_cosf(yaw) * SDL_sinf(pitch);
    pose->roll = SDL_atan2f(sine, cosine) / radians;
    pose->fov = clamp(work->projection.vertical_fov, 5.f, 150.f);
    pose->near_clip = clamp(work->projection.near_clip, .01f, 100.f);
    pose->far_clip = clamp(work->projection.far_clip, pose->near_clip + .01f, 32700.f);
    return 1;
}

void marcus_board_projection(const MarcusBoardPose *pose, MarcusProjection *projection)
{
    float cy, sy, cp, sp, cr, sr, *matrix;
    cy = SDL_cosf(pose->camera.yaw * radians);
    sy = SDL_sinf(pose->camera.yaw * radians);
    cp = SDL_cosf(pose->camera.pitch * radians);
    sp = SDL_sinf(pose->camera.pitch * radians);
    cr = SDL_cosf(pose->roll * radians);
    sr = SDL_sinf(pose->roll * radians);
    memset(projection, 0, sizeof(*projection));
    matrix = projection->transform;
    matrix[0] = cy * cr - sy * sp * sr;
    matrix[1] = cp * sr;
    matrix[2] = -sy * cr - cy * sp * sr;
    matrix[3] = -cy * sr - sy * sp * cr;
    matrix[4] = cp * cr;
    matrix[5] = sy * sr - cy * sp * cr;
    matrix[6] = sy * cp;
    matrix[7] = sp;
    matrix[8] = cy * cp;
    matrix[9] = pose->camera.x;
    matrix[10] = pose->camera.y;
    matrix[11] = pose->camera.z;
    projection->valid = 1;
    projection->vertical_fov = pose->fov;
    projection->aspect = (float)MARCUS_BOARD_WIDTH / MARCUS_BOARD_HEIGHT;
    projection->near_clip = pose->near_clip;
    projection->far_clip = pose->far_clip;
    projection->display_depth = 10;
}

void marcus_board_light_matrix(const MarcusBoardLightValues *light, float matrix[16])
{
    float sy, cy, sp, cp;
    sy = SDL_sinf(light->yaw * radians);
    cy = SDL_cosf(light->yaw * radians);
    sp = SDL_sinf(light->pitch * radians);
    cp = SDL_cosf(light->pitch * radians);
    matrix[0] = cy;
    matrix[1] = 0;
    matrix[2] = -sy;
    matrix[3] = 0;
    matrix[4] = -sy * sp;
    matrix[5] = cp;
    matrix[6] = -cy * sp;
    matrix[7] = 0;
    matrix[8] = sy * cp;
    matrix[9] = sp;
    matrix[10] = cy * cp;
    matrix[11] = 0;
    matrix[12] = light->x;
    matrix[13] = light->y;
    matrix[14] = light->z;
    matrix[15] = 1;
}

int marcus_board_light_from_matrix(const MarcusBoardLightValues *original, const float matrix[16], int rotate,
                                   MarcusBoardLightValues *light)
{
    MarcusBoardLightValues next;
    float xy, length;
    int i;
    for (i = 0; i < 16; ++i)
        if (!(matrix[i] >= -FLT_MAX && matrix[i] <= FLT_MAX)) return 0;
    next = *original;
    if (rotate)
    {
        if (original->point) return 0;
        xy = (float)sqrt((double)matrix[8] * matrix[8] + (double)matrix[9] * matrix[9]);
        length = (float)sqrt((double)xy * xy + (double)matrix[10] * matrix[10]);
        if (length < .0001f) return 0;
        next.yaw = SDL_atan2f(matrix[8], matrix[10]) / radians;
        next.pitch = clamp(SDL_asinf(clamp(matrix[9] / length, -1.f, 1.f)) / radians, -89.f, 89.f);
    }
    else
    {
        next.x = clamp(matrix[12], -32700.f, 32700.f);
        next.y = clamp(matrix[13], -32700.f, 32700.f);
        next.z = clamp(matrix[14], -32700.f, 32700.f);
    }
    *light = next;
    return 1;
}

void marcus_board_stroke(unsigned char *pixels, int x0, int y0, int x1, int y1, int size, unsigned char colour)
{
    int dx, dy, sx, sy, error, twice, x, y, px, py;
    if (!pixels || size < 1 || size > 64 || x0 < 0 || x0 >= MARCUS_BOARD_WIDTH || x1 < 0 || x1 >= MARCUS_BOARD_WIDTH ||
        y0 < 0 || y0 >= MARCUS_BOARD_HEIGHT || y1 < 0 || y1 >= MARCUS_BOARD_HEIGHT) return;
    dx = abs(x1 - x0);
    dy = -abs(y1 - y0);
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    error = dx + dy;
    for (;;)
    {
        for (y = -(size / 2); y < (size + 1) / 2; ++y)
            for (x = -(size / 2); x < (size + 1) / 2; ++x)
            {
                /* A blunt, chipped square brush produces opaque ink pixels. */
                if (size > 3 && abs(x) + abs(y) > size - 2) continue;
                px = x0 + x;
                py = y0 + y;
                if (px >= 0 && px < MARCUS_BOARD_WIDTH && py >= 0 && py < MARCUS_BOARD_HEIGHT)
                    pixels[py * MARCUS_BOARD_WIDTH + px] = colour;
            }
        if (x0 == x1 && y0 == y1) break;
        twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

int marcus_board_colour_slot(const unsigned *colours, int count, unsigned rgb,
                             const unsigned char *const *surfaces, int angle_count)
{
    unsigned char used[MARCUS_BOARD_COLOUR_LIMIT] = {0};
    int i, pixel;
    if (rgb > 0xffffff) return -1;
    for (i = 0; i < count; ++i) if (colours[i] == rgb) return i;
    if (count < MARCUS_BOARD_COLOUR_LIMIT) return count;
    for (i = 0; i < angle_count; ++i)
        for (pixel = 0; pixel < MARCUS_BOARD_PIXELS; ++pixel) used[surfaces[i][pixel]] = 1;
    for (i = MARCUS_BOARD_SWATCH_COUNT; i < MARCUS_BOARD_COLOUR_LIMIT; ++i) if (!used[i]) return i;
    return -1;
}
