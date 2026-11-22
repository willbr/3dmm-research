#include "storyboard_edit_data.h"
#include "mm/background_resource.h"
#include <SDL3/SDL.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

const unsigned marcus_board_swatches[MARCUS_BOARD_SWATCH_COUNT] = {
    0xd3d4d6, 0x000000, 0xffffff, 0x9ebfd9, 0xdea5a2, 0xe6d8a0, 0xaac9ad, 0x505050, 0x949494, 0xc4c4c4
};
typedef char BoardFloatWidth[sizeof(float) == 4 && sizeof(unsigned) == 4 ? 1 : -1];

static int board_range(float value, float low, float high)
{
    return value >= low && value <= high;
}

static int valid_wire(const MarcusBoardWire *board)
{
    const MarcusBoardEditValues *v = &board->values;
    const MarcusBoardPose *pose;
    const MarcusBoardLightValues *light;
    int i, j;
    if (v->angle_count < 1 || v->angle_count > MARCUS_BOARD_ANGLE_LIMIT ||
        v->light_count < 0 || v->light_count > MARCUS_BOARD_LIGHT_LIMIT ||
        v->colour_count < MARCUS_BOARD_SWATCH_COUNT || v->colour_count > MARCUS_BOARD_COLOUR_LIMIT ||
        memcmp(v->colours, marcus_board_swatches, sizeof(marcus_board_swatches)))
        return 0;
    for (i = 0; i < v->colour_count; ++i)
        if (v->colours[i] > 0xffffffU)
            return 0;
    for (i = 0; i < v->angle_count; ++i)
    {
        pose = &v->angles[i];
        if (!board->pixels[i] || !board_range(pose->camera.x, -32700, 32700) ||
            !board_range(pose->camera.y, -32700, 32700) || !board_range(pose->camera.z, -32700, 32700) ||
            !board_range(pose->camera.yaw, -360, 360) || !board_range(pose->camera.pitch, -90, 90) ||
            !board_range(pose->roll, -180, 180) || !board_range(pose->fov, 5, 150) ||
            !board_range(pose->near_clip, .01f, 100) || !board_range(pose->far_clip, pose->near_clip + .01f, 32700))
            return 0;
        for (j = 0; j < MARCUS_BOARD_PIXELS; ++j)
            if (board->pixels[i][j] >= v->colour_count)
                return 0;
    }
    for (i = 0; i < v->light_count; ++i)
    {
        light = &v->lights[i];
        if (!mm_background_light_values_valid(light) || (light->point != 0 && light->point != 1))
            return 0;
    }
    return 1;
}

static unsigned get_word(const unsigned char **at)
{
    const unsigned char *p = *at;
    *at += 4;
    return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}
static float get_float(const unsigned char **at)
{
    unsigned bits = get_word(at);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
static void put_word(unsigned char **at, unsigned value)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        *(*at)++ = (unsigned char)(value >> (8 * i));
}
static void put_float(unsigned char **at, float value)
{
    unsigned bits;
    memcpy(&bits, &value, sizeof(bits));
    put_word(at, bits);
}

static size_t board_bytes(unsigned angles, unsigned lights)
{
    return (size_t)angles * (36 + MARCUS_BOARD_PIXELS) + (size_t)lights * 28;
}

int marcus_board_decode(const unsigned char *bytes, size_t length, MarcusBoardWire *out)
{
    MarcusBoardWire decoded;
    MarcusBoardEditValues *v = &decoded.values;
    const unsigned char *at = bytes;
    unsigned version, angles, lights, colours, i, j, point;
    if (length < 20)
        return MARCUS_BOARD_WIRE_SIZE;
    version = get_word(&at);
    if (version < 1 || version > 3 || get_word(&at) != MARCUS_BOARD_WIDTH || get_word(&at) != MARCUS_BOARD_HEIGHT)
        return MARCUS_BOARD_WIRE_FORMAT;
    angles = get_word(&at);
    lights = get_word(&at);
    if (!angles || angles > MARCUS_BOARD_ANGLE_LIMIT || lights > MARCUS_BOARD_LIGHT_LIMIT)
        return MARCUS_BOARD_WIRE_SIZE;
    memset(&decoded, 0, sizeof(decoded));
    v->angle_count = (int)angles;
    v->light_count = (int)lights;
    if (version >= 3)
    {
        if (length < 24)
            return MARCUS_BOARD_WIRE_SIZE;
        colours = get_word(&at);
        if (colours < MARCUS_BOARD_SWATCH_COUNT || colours > MARCUS_BOARD_COLOUR_LIMIT)
            return MARCUS_BOARD_WIRE_PALETTE;
        if (length != 24 + 4 * colours + board_bytes(angles, lights))
            return MARCUS_BOARD_WIRE_SIZE;
        v->colour_count = (int)colours;
        for (i = 0; i < colours; ++i) v->colours[i] = get_word(&at);
    }
    else
    {
        if (length != 20 + board_bytes(angles, lights))
            return MARCUS_BOARD_WIRE_SIZE;
        v->colour_count = MARCUS_BOARD_SWATCH_COUNT;
        memcpy(v->colours, marcus_board_swatches, sizeof(marcus_board_swatches));
    }
    for (i = 0; i < angles; ++i)
    {
        MarcusBoardPose *pose = &v->angles[i];
        pose->camera.x = get_float(&at); pose->camera.y = get_float(&at); pose->camera.z = get_float(&at);
        pose->camera.yaw = get_float(&at); pose->camera.pitch = get_float(&at); pose->roll = get_float(&at);
        pose->fov = get_float(&at); pose->near_clip = get_float(&at); pose->far_clip = get_float(&at);
        decoded.pixels[i] = at;
        if (version == 1)
            for (j = 0; j < MARCUS_BOARD_PIXELS; ++j)
                if (at[j] > 6)
                    return MARCUS_BOARD_WIRE_LEGACY_COLOUR;
        at += MARCUS_BOARD_PIXELS;
    }
    for (i = 0; i < lights; ++i)
    {
        MarcusBoardLightValues *light = &v->lights[i];
        light->x = get_float(&at); light->y = get_float(&at); light->z = get_float(&at);
        light->yaw = get_float(&at); light->pitch = get_float(&at); light->intensity = get_float(&at);
        point = get_word(&at);
        if (point > 1)
            return MARCUS_BOARD_WIRE_VALUES;
        light->point = (int)point;
    }
    if (!valid_wire(&decoded))
        return MARCUS_BOARD_WIRE_VALUES;
    *out = decoded;
    return MARCUS_BOARD_WIRE_OK;
}

size_t marcus_board_wire_size(const MarcusBoardWire *board)
{
    const MarcusBoardEditValues *v = &board->values;
    return valid_wire(board) ? 24 + 4 * v->colour_count + board_bytes(v->angle_count, v->light_count) : 0;
}

int marcus_board_encode(const MarcusBoardWire *board, unsigned char *bytes, size_t length)
{
    const MarcusBoardEditValues *v = &board->values;
    int i;
    size_t expected = marcus_board_wire_size(board);
    if (!expected || length != expected)
        return 0;
    put_word(&bytes, 3); put_word(&bytes, MARCUS_BOARD_WIDTH); put_word(&bytes, MARCUS_BOARD_HEIGHT);
    put_word(&bytes, v->angle_count); put_word(&bytes, v->light_count); put_word(&bytes, v->colour_count);
    for (i = 0; i < v->colour_count; ++i) put_word(&bytes, v->colours[i]);
    for (i = 0; i < v->angle_count; ++i)
    {
        const MarcusBoardPose *pose = &v->angles[i];
        put_float(&bytes, pose->camera.x); put_float(&bytes, pose->camera.y); put_float(&bytes, pose->camera.z);
        put_float(&bytes, pose->camera.yaw); put_float(&bytes, pose->camera.pitch); put_float(&bytes, pose->roll);
        put_float(&bytes, pose->fov); put_float(&bytes, pose->near_clip); put_float(&bytes, pose->far_clip);
        memcpy(bytes, board->pixels[i], MARCUS_BOARD_PIXELS);
        bytes += MARCUS_BOARD_PIXELS;
    }
    for (i = 0; i < v->light_count; ++i)
    {
        const MarcusBoardLightValues *light = &v->lights[i];
        put_float(&bytes, light->x); put_float(&bytes, light->y); put_float(&bytes, light->z);
        put_float(&bytes, light->yaw); put_float(&bytes, light->pitch); put_float(&bytes, light->intensity);
        put_word(&bytes, light->point);
    }
    return 1;
}
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
