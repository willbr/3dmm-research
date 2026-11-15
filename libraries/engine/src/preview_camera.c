#include "mm/preview_camera.h"
#include <float.h>
#include <string.h>
static float minf(float a, float b)
{
    return b < a ? b : a;
}
static float maxf(float a, float b)
{
    return a < b ? b : a;
}
static int finitef(float x)
{
    return x >= -FLT_MAX && x <= FLT_MAX;
}
int mm_preview_camera_match(float aspect, MMPreviewOrbit *orbit, MMPreviewCameraPose *out,
                            const MMPreviewRenderServices *s, float (*atan_value)(float))
{
    MMPreviewData d;
    MMPreviewBody body;
    const MMPreviewMath *m = &s->math;
    float right[3] = {1, 0, 0}, backward[3] = {0, 0, 1}, base_right[3], base_up[3];
    float fov, scale, yaw, pitch, cosine = 0, sine = 0, focus = 10, low[3] = {0, 0, 0}, high[3] = {0, 0, 0};
    MMU32 i, n;
    int axis, first = 1;
    memset(out, 0, sizeof(*out));
    out->move_speed = 1;
    out->vertical_fov = 45;
    if (!s->ready(s->context) || !finitef(aspect) || aspect <= 0)
        return MM_PREVIEW_CAMERA_INVALID;
    memset(&d, 0, sizeof(d));
    s->describe(s->context, &d);
    if (!d.scene)
        return MM_PREVIEW_CAMERA_INVALID;
    out->position[2] = 10;
    if (d.background)
    {
        for (axis = 0; axis < 3; ++axis)
        {
            out->position[axis] = m->scalar_float(d.camera.matrix[9 + axis]);
            right[axis] = m->scalar_float(d.camera.matrix[axis]);
            backward[axis] = m->scalar_float(d.camera.matrix[6 + axis]);
        }
        fov = (float)d.camera.fov * (360.f / 65536.f);
        scale = maxf(1.f, ((float)d.width / d.height) / aspect);
        out->vertical_fov = 2 * atan_value(m->tan(fov * .00872664626f) * scale) * 57.2957795f;
    }
    out->yaw = m->atan2(backward[0], backward[2]) * 57.2957795f;
    out->pitch = m->asin(maxf(-1.f, minf(1.f, backward[1]))) * 57.2957795f;
    yaw = out->yaw * .01745329252f;
    pitch = out->pitch * .01745329252f;
    base_right[0] = m->cos(yaw);
    base_right[1] = 0;
    base_right[2] = -m->sin(yaw);
    base_up[0] = -m->sin(yaw) * m->sin(pitch);
    base_up[1] = m->cos(pitch);
    base_up[2] = -m->cos(yaw) * m->sin(pitch);
    for (axis = 0; axis < 3; ++axis)
    {
        cosine += right[axis] * base_right[axis];
        sine += right[axis] * base_up[axis];
    }
    out->roll = m->atan2(sine, cosine) * 57.2957795f;
    n = s->body_count(s->context);
    for (i = 0; i < n; ++i)
        if (s->body(s->context, i, &body))
        {
            for (axis = 0; axis < 3; ++axis)
            {
                low[axis] = first ? body.low[axis] : minf(low[axis], body.low[axis]);
                high[axis] = first ? body.high[axis] : maxf(high[axis], body.high[axis]);
            }
            first = 0;
        }
    if (!first)
    {
        focus = 0;
        for (axis = 0; axis < 3; ++axis)
            focus += (out->position[axis] - (low[axis] + high[axis]) * .5f) * backward[axis];
        focus = maxf(1.f, minf(10000.f, focus));
    }
    for (axis = 0; axis < 3; ++axis)
        orbit->center[axis] = out->position[axis] - backward[axis] * focus;
    orbit->radius = focus * minf(1.f, aspect) / 2.8f;
    orbit->framed = orbit->includes_camera = 1;
    orbit->object = -1;
    out->move_speed = maxf(.1f, minf(1000.f, focus * .25f));
    return MM_PREVIEW_CAMERA_OK;
}
int mm_preview_orbit_pan(const MMPreviewView *v, float right, float up, MMPreviewOrbit *orbit, int ready,
                         const MMPreviewMath *m)
{
    float aspect, distance, height, yaw, pitch, sy, cy, sp, cp, cr, sr, backward[3], r[3], u[3], delta[3];
    float fraction = 1, position;
    int axis;
    if (!ready || !orbit->framed || v->width < 64 || v->height < 64 || v->width > 4096 || v->height > 4096 ||
        !finitef(right) || !finitef(up) || !finitef(v->yaw) || !finitef(v->pitch) || !finitef(v->roll) ||
        !finitef(v->vertical_fov) || v->vertical_fov <= 0 || v->vertical_fov >= 179 || !finitef(v->zoom) ||
        v->pitch < -90 || v->pitch > 90 || v->zoom < .1f || v->zoom > 10)
        return MM_PREVIEW_CAMERA_INVALID;
    aspect = (float)v->width / v->height;
    distance = orbit->radius * 2.8f / minf(1.f, aspect) / v->zoom;
    height =
        v->orthographic ? 2 * orbit->radius * 1.15f / v->zoom : 2 * distance * m->tan(v->vertical_fov * .00872664626f);
    yaw = m->fmod(v->yaw, 360.f) * .01745329252f;
    pitch = v->pitch * .01745329252f;
    sy = m->sin(yaw);
    cy = m->cos(yaw);
    sp = m->sin(pitch);
    cp = m->cos(pitch);
    backward[0] = sy * cp;
    backward[1] = sp;
    backward[2] = cy * cp;
    cr = m->cos(v->roll * .01745329252f);
    sr = m->sin(v->roll * .01745329252f);
    r[0] = cy * cr - sy * sp * sr;
    r[1] = cp * sr;
    r[2] = -sy * cr - cy * sp * sr;
    u[0] = -sy * sp * cr - cy * sr;
    u[1] = cp * cr;
    u[2] = -cy * sp * cr + sy * sr;
    for (axis = 0; axis < 3; ++axis)
        delta[axis] = height * (right * aspect * r[axis] + up * u[axis]);
    for (axis = 0; axis < 3; ++axis)
    {
        if (!finitef(delta[axis]))
            return MM_PREVIEW_CAMERA_PAN_RANGE;
        position = orbit->center[axis] + backward[axis] * distance;
        if (delta[axis] > 0)
            fraction = minf(fraction, (32700 - position) / delta[axis]);
        else if (delta[axis] < 0)
            fraction = minf(fraction, (-32700 - position) / delta[axis]);
    }
    for (axis = 0; axis < 3; ++axis)
        orbit->center[axis] += delta[axis] * maxf(0.f, fraction);
    return MM_PREVIEW_CAMERA_OK;
}
void mm_preview_orbit_reset(MMPreviewOrbit *o)
{
    o->framed = 0;
}
int mm_preview_orbit_follows(const MMPreviewOrbit *o, MMI32 object, int visible)
{
    return o->framed && o->object == object && visible;
}
void mm_preview_orbit_move(MMPreviewOrbit *o, MMI32 object, const float before[3], const float after[3])
{
    int axis;
    if (mm_preview_orbit_follows(o, object, 1))
        for (axis = 0; axis < 3; ++axis)
            o->anchor[axis] += after[axis] - before[axis];
}
void mm_preview_orbit_resize(MMPreviewOrbit *o, const MMI32 before[6], const MMI32 after[6], const MMPreviewMath *m)
{
    int i;
    for (i = 0; i < 3; ++i)
        o->anchor[i] += (m->scalar_float(after[i]) - m->scalar_float(before[i]) + m->scalar_float(after[i + 3]) -
                         m->scalar_float(before[i + 3])) *
                        .5f;
}
int mm_preview_camera_bounds(const MMI32 low[3], const MMI32 high[3], const MMI32 matrix[12], float corners[24],
                             const MMPreviewMath *m)
{
    int axis, corner, local;
    double position;
    memset(corners, 0, 24 * sizeof(*corners));
    for (axis = 0; axis < 3; ++axis)
        if (low[axis] > high[axis])
            return 0;
    for (corner = 0; corner < 8; ++corner)
        for (axis = 0; axis < 3; ++axis)
        {
            position = m->scalar_float(matrix[9 + axis]);
            for (local = 0; local < 3; ++local)
                position += m->scalar_float((corner & (1 << local)) ? high[local] : low[local]) *
                            (double)m->scalar_float(matrix[local * 3 + axis]);
            corners[corner * 3 + axis] = (float)position;
        }
    return 1;
}
int mm_preview_camera_admit(int ready, int background, MMU32 count, MMU32 index)
{
    return ready && background && index < count;
}
int mm_preview_camera_select(int *ready, MMU32 *current, int background, MMU32 count, MMU32 index, void *context,
                             int (*select)(void *, MMU32))
{
    int result;
    if (!mm_preview_camera_admit(*ready, background, count, index))
        return MM_PREVIEW_CAMERA_INVALID;
    if (index == *current)
        return MM_PREVIEW_CAMERA_OK;
    result = select(context, index);
    if (result < 0)
        return MM_PREVIEW_CAMERA_MEMORY;
    if (!result)
    {
        *ready = 0;
        return MM_PREVIEW_CAMERA_READ;
    }
    *current = index;
    return MM_PREVIEW_CAMERA_OK;
}
int mm_preview_camera_placement(int ready, const MMI32 place[3], float point[3], const MMPreviewMath *m)
{
    int i;
    if (!ready || !place || !point)
        return 0;
    for (i = 0; i < 3; ++i)
        point[i] = m->scalar_float(place[i]);
    return 1;
}
int mm_preview_projection_cache_init(MMPreviewProjectionCache *c, const MMStorageAllocator *a)
{
    memset(c, 0, sizeof(*c));
    return mm_storage_init(&c->storage, a) == MM_STORAGE_OK;
}
void mm_preview_projection_cache_reset(MMPreviewProjectionCache *c)
{
    c->count = 0;
}
void mm_preview_projection_cache_dispose(MMPreviewProjectionCache *c)
{
    mm_storage_dispose(&c->storage);
    c->count = 0;
}
int mm_preview_projection_cache_get(MMPreviewProjectionCache *c, const MMU32 *numbers, MMU32 count, MMU32 index,
                                    MMPreviewProjection *out, const MMBackgroundPreviewServices *s)
{
    MMPreviewProjection *p;
    unsigned char *read;
    MMReadStream stream;
    MMBackgroundCameraData camera;
    MMU32 child, width, height;
    MMI32 bounds[4];
    int row, axis;
    void *bitmap;
    memset(out, 0, sizeof(*out));
    if (index >= count)
        return MM_PREVIEW_CAMERA_READ;
    if (c->count != count)
    {
        if (count > MM_STORAGE_LIMIT / sizeof(*p) ||
            mm_storage_ensure(&c->storage, count * sizeof(*p), count, 0) != MM_STORAGE_OK)
            return MM_PREVIEW_CAMERA_MEMORY;
        memset(c->storage.buffers[0].data, 0, count * sizeof(*p));
        memset(c->storage.buffers[1].data, 0, count);
        c->count = count;
    }
    p = (MMPreviewProjection *)c->storage.buffers[0].data + index;
    read = c->storage.buffers[1].data + index;
    if (!*read)
    {
        *read = 2;
        if (!s->camera_read(s->context, numbers[index], &stream) || !mm_background_camera_decode(&stream, &camera) ||
            !s->child(s->context, numbers[index], 0, &child) || !(bitmap = s->fetch(s->context, child, 0)))
            return MM_PREVIEW_CAMERA_READ;
        s->bounds(s->context, bitmap, bounds);
        s->release(s->context, bitmap);
        if (bounds[2] <= bounds[0] || bounds[3] <= bounds[1])
            return MM_PREVIEW_CAMERA_READ;
        width = (MMU32)bounds[2] - (MMU32)bounds[0];
        height = (MMU32)bounds[3] - (MMU32)bounds[1];
        if (width > 2147483647U || height > 2147483647U)
            return MM_PREVIEW_CAMERA_READ;
        for (row = 0; row < 4; ++row)
            for (axis = 0; axis < 3; ++axis)
                p->transform[row * 3 + axis] = s->scalar_float(s->context, camera.matrix.m[row][axis]);
        p->vertical_fov = camera.fov * (360.f / 65536.f);
        p->near_clip = s->scalar_float(s->context, camera.near_z);
        p->far_clip = s->scalar_float(s->context, camera.far_z);
        p->aspect = (float)width / height;
        p->display_depth = 10.f;
        p->valid = p->vertical_fov > 0 && p->vertical_fov < 179 && p->near_clip > 0 && p->far_clip > p->near_clip;
        *read = p->valid ? 1 : 2;
    }
    *out = *p;
    return *read == 1 ? MM_PREVIEW_CAMERA_OK : MM_PREVIEW_CAMERA_READ;
}
