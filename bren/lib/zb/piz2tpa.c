/* Opaque indexed perspective triangles for the BRender scene pipeline.
 * Input vertices have already been transformed, lit and clipped by BRender.
 * U/W, V/W and 1/W vary linearly over screen space; Z uses its native 1.31
 * projected fraction. This path deliberately bypasses lighting and zero-key
 * transparency for prerendered artwork. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "zb.h"

struct perspective_vertex
{
    double x, y, z, q, u, v;
};
struct perspective_parameter
{
    double origin, dx, dy;
};
static struct perspective_parameter parameter(double a, double b, double c, double abx, double aby, double acx,
                                              double acy, double inverse_area)
{
    struct perspective_parameter result;
    result.origin = a;
    result.dx = ((b - a) * acy - (c - a) * aby) * inverse_area;
    result.dy = ((c - a) * abx - (b - a) * acx) * inverse_area;
    return result;
}

void BR_ASM_CALL TriangleRenderPIZ2TPA(struct temp_vertex_fixed *a, struct temp_vertex_fixed *b,
                                       struct temp_vertex_fixed *c)
{
    const struct temp_vertex_fixed *input[3] = {a, b, c};
    struct perspective_vertex p[3];
    const br_pixelmap *map = zb.material->colour_map;
    double min_y, max_y, abx, aby, acx, acy, area, inverse_area;
    struct perspective_parameter z, q, u, v;
    int i, y, top, bottom;
    for (i = 0; i < 3; ++i)
    {
        double w = input[i]->comp[C_W] / 65536.0;
        if (!(w > 0))
            return;
        p[i].x = input[i]->v[0] / 65536.0;
        p[i].y = input[i]->v[1] / 65536.0;
        p[i].z = input[i]->v[2] / 65536.0;
        p[i].q = 1.0 / w;
        p[i].u = input[i]->comp[C_U] / 65536.0 * p[i].q;
        p[i].v = input[i]->comp[C_V] / 65536.0 * p[i].q;
    }
    abx = p[1].x - p[0].x;
    aby = p[1].y - p[0].y;
    acx = p[2].x - p[0].x;
    acy = p[2].y - p[0].y;
    area = abx * acy - acx * aby;
    if (!isfinite(area) || fabs(area) < 1e-9)
        return;
    inverse_area = 1.0 / area;
    z = parameter(p[0].z, p[1].z, p[2].z, abx, aby, acx, acy, inverse_area);
    q = parameter(p[0].q, p[1].q, p[2].q, abx, aby, acx, acy, inverse_area);
    u = parameter(p[0].u, p[1].u, p[2].u, abx, aby, acx, acy, inverse_area);
    v = parameter(p[0].v, p[1].v, p[2].v, abx, aby, acx, acy, inverse_area);
    min_y = max_y = p[0].y;
    for (i = 1; i < 3; ++i)
    {
        if (p[i].y < min_y)
            min_y = p[i].y;
        if (p[i].y > max_y)
            max_y = p[i].y;
    }
    top = (int)ceil(min_y - .5 - 1e-7);
    bottom = (int)floor(max_y - .5 + 1e-7);
    if (top < 0)
        top = 0;
    if (bottom >= fw.output->height)
        bottom = fw.output->height - 1;
    for (y = top; y <= bottom; ++y)
    {
        double sample_y = y + .5;
        double left = 1e30, right = -1e30;
        double offset_x, offset_y, current_z, current_q, current_u, current_v;
        int first, last, x;
        uint8_t *pixels;
        uint16_t *depth;
        /* Intersect the three edges with this pixel-centre scanline. Walking
         * a span avoids testing every point in a triangle's bounding box. */
        for (i = 0; i < 3; ++i)
        {
            const struct perspective_vertex *start = &p[i];
            const struct perspective_vertex *end = &p[(i + 1) % 3];
            double low = start->y < end->y ? start->y : end->y;
            double high = start->y > end->y ? start->y : end->y;
            double intersection;
            if (sample_y < low - 1e-7 || sample_y > high + 1e-7)
                continue;
            if (fabs(end->y - start->y) < 1e-9)
            {
                if (start->x < left)
                    left = start->x;
                if (start->x > right)
                    right = start->x;
                if (end->x < left)
                    left = end->x;
                if (end->x > right)
                    right = end->x;
                continue;
            }
            intersection = start->x + (sample_y - start->y) * (end->x - start->x) / (end->y - start->y);
            if (intersection < left)
                left = intersection;
            if (intersection > right)
                right = intersection;
        }
        if (left > right)
            continue;
        first = (int)ceil(left - .5 - 1e-7);
        last = (int)floor(right - .5 + 1e-7);
        if (first < fw.output->base_x)
            first = fw.output->base_x;
        if (last >= fw.output->base_x + fw.output->width)
            last = fw.output->base_x + fw.output->width - 1;
        offset_x = first + .5 - p[0].x;
        offset_y = sample_y - p[0].y;
        current_z = z.origin + z.dx * offset_x + z.dy * offset_y;
        current_q = q.origin + q.dx * offset_x + q.dy * offset_y;
        current_u = u.origin + u.dx * offset_x + u.dy * offset_y;
        current_v = v.origin + v.dx * offset_x + v.dy * offset_y;
        pixels = (uint8_t *)zb.colour_buffer + (size_t)y * zb.row_width;
        depth = (uint16_t *)((uint8_t *)zb.depth_buffer + (size_t)y * zb.depth_row_width);
        for (x = first; x <= last; ++x)
        {
            double clamped_z = current_z < 0 ? 0 : current_z > 65535 ? 65535 : current_z;
            uint16_t z16 = (uint16_t)clamped_z;
            if (z16 <= depth[x] && current_q > 0)
            {
                double reciprocal = 1.0 / current_q;
                int tx = (int)floor(current_u * reciprocal);
                int ty = (int)floor(current_v * reciprocal);
                if (tx < 0 || tx >= map->width)
                {
                    tx %= map->width;
                    if (tx < 0)
                        tx += map->width;
                }
                if (ty < 0 || ty >= map->height)
                {
                    ty %= map->height;
                    if (ty < 0)
                        ty += map->height;
                }
                pixels[x] = ((uint8_t *)map->pixels)[(size_t)(ty + map->base_y) * map->row_bytes + tx + map->base_x];
                depth[x] = z16;
            }
            current_z += z.dx;
            current_q += q.dx;
            current_u += u.dx;
            current_v += v.dx;
        }
    }
}
