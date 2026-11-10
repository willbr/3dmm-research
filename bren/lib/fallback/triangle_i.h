/* Portable form of the integer Gouraud scanner in zb/ti8_piz.asm.
 * Original algorithm Copyright (c) 1992,1993-1995 Argonaut Technologies Limited.
 * All rights reserved. Preserve the assembly's integer edges, reciprocal table,
 * half-X parameter bias, winding and strict depth comparison. */
extern unsigned long _reciprocal[2048];

struct integer_parameter
{
    uint32_t current, dx, no_carry, carry;
};

static uint32_t integer_divide(int64_t numerator, int32_t divisor)
{
    uint64_t magnitude = numerator < 0 ? (uint64_t)-numerator : (uint64_t)numerator;
    uint32_t denominator = divisor < 0 ? 0u - (uint32_t)divisor : (uint32_t)divisor;
    uint32_t quotient;
    /* GDIVIDE leaves the absolute numerator's low word on overflow. */
    if ((magnitude >> 32) >= denominator)
        return (uint32_t)magnitude;
    quotient = (uint32_t)(magnitude / denominator);
    return (numerator < 0) != (divisor < 0) ? 0u - quotient : quotient;
}

static struct integer_parameter integer_parameter_setup(uint32_t a, uint32_t b, uint32_t c, int32_t long_x,
                                                        int32_t long_y, int32_t top_x, int32_t top_y, int32_t divisor,
                                                        int32_t long_gradient)
{
    struct integer_parameter result;
    int32_t ab = (int32_t)(b - a), ac = (int32_t)(c - a);
    uint32_t dy;
    result.dx = integer_divide((int64_t)ab * long_y - (int64_t)ac * top_y, divisor);
    dy = integer_divide((int64_t)ac * top_x - (int64_t)ab * long_x, divisor);
    result.current = a + (uint32_t)((int32_t)result.dx >> 1) + (result.dx & 1);
    result.no_carry = (uint32_t)(long_gradient >> 16) * result.dx + dy;
    result.carry = result.no_carry + result.dx;
    return result;
}

static int32_t integer_edge_gradient(int32_t dx, int32_t dy)
{
    uint32_t reciprocal = dy < 2048 ? (uint32_t)_reciprocal[dy] : 65536u / (uint32_t)dy;
    return (int32_t)((uint32_t)dx * reciprocal);
}

void TriangleRenderPIZ2I(struct temp_vertex_fixed *v0, struct temp_vertex_fixed *v1, struct temp_vertex_fixed *v2)
{
    struct temp_vertex_fixed *vertices[3] = {v0, v1, v2}, *temporary;
    int32_t x[3], y[3], long_x, long_y, top_x, top_y, divisor, long_gradient;
    int32_t long_position, half;
    uint32_t long_fraction = 0x80000000u;
    struct integer_parameter z, intensity;
#define SWAP_I(a, b)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        temporary = vertices[a];                                                                                       \
        vertices[a] = vertices[b];                                                                                     \
        vertices[b] = temporary;                                                                                       \
    } while (0)
    /* The tie order here is part of SETUP_PI, before subpixel truncation. */
    if (vertices[0]->v[1] > vertices[1]->v[1])
    {
        if (vertices[2]->v[1] <= vertices[1]->v[1])
        {
            SWAP_I(0, 2);
        }
        else if (vertices[0]->v[1] <= vertices[2]->v[1])
        {
            SWAP_I(0, 1);
        }
        else
        {
            SWAP_I(0, 1);
            SWAP_I(1, 2);
        }
    }
    else if (vertices[1]->v[1] > vertices[2]->v[1])
    {
        if (vertices[0]->v[1] <= vertices[2]->v[1])
        {
            SWAP_I(1, 2);
        }
        else
        {
            SWAP_I(1, 2);
            SWAP_I(0, 1);
        }
    }
#undef SWAP_I
    if (!fw.output || zb.row_width <= 0 || zb.depth_row_width <= 0)
        return;
    for (half = 0; half < 3; ++half)
    {
        x[half] = vertices[half]->v[0] >> 16;
        y[half] = vertices[half]->v[1] >> 16;
    }
    long_x = x[2] - x[0];
    long_y = y[2] - y[0];
    top_x = x[1] - x[0];
    top_y = y[1] - y[0];
    if (!long_y)
        return;
    divisor = (int32_t)((uint32_t)top_x * (uint32_t)long_y - (uint32_t)long_x * (uint32_t)top_y);
    if (!divisor)
        return;
    long_gradient = integer_edge_gradient(long_x, long_y);
    z = integer_parameter_setup(vertices[0]->v[2], vertices[1]->v[2], vertices[2]->v[2], long_x, long_y, top_x, top_y,
                                divisor, long_gradient);
    intensity = integer_parameter_setup(vertices[0]->comp[C_I], vertices[1]->comp[C_I], vertices[2]->comp[C_I], long_x,
                                        long_y, top_x, top_y, divisor, long_gradient);
    long_position = x[0];
    for (half = 0; half < 2; ++half)
    {
        int32_t scan_y, minor_position = x[half], height = y[half + 1] - y[half];
        int32_t minor_gradient;
        uint32_t minor_fraction = 0x80000000u;
        if (!height)
            continue;
        minor_gradient = integer_edge_gradient(x[half + 1] - x[half], height);
        for (scan_y = y[half]; scan_y < y[half + 1]; ++scan_y)
        {
            if (scan_y >= 0 && scan_y < fw.output->height)
            {
                int32_t first = divisor > 0 ? long_position : minor_position;
                int32_t end = divisor > 0 ? minor_position : long_position;
                int32_t pixel;
                uint8_t *colour = (uint8_t *)zb.colour_buffer + (size_t)scan_y * zb.row_width;
                uint16_t *depth = (uint16_t *)((uint8_t *)zb.depth_buffer + (size_t)scan_y * zb.depth_row_width);
                if (first < fw.output->base_x)
                    first = fw.output->base_x;
                if (end > fw.output->base_x + fw.output->width)
                    end = fw.output->base_x + fw.output->width;
                for (pixel = first; pixel < end; ++pixel)
                {
                    uint32_t offset = (uint32_t)(pixel - long_position);
                    uint16_t value = (uint16_t)((z.current + offset * z.dx) >> 16);
                    if (value < depth[pixel])
                    {
                        depth[pixel] = value;
                        colour[pixel] = (uint8_t)((intensity.current + offset * intensity.dx) >> 16);
                    }
                }
            }
            {
                uint32_t previous = minor_fraction;
                minor_fraction += (uint32_t)minor_gradient << 16;
                minor_position += (minor_gradient >> 16) + (minor_fraction < previous);
                previous = long_fraction;
                long_fraction += (uint32_t)long_gradient << 16;
                long_position += (long_gradient >> 16) + (long_fraction < previous);
                z.current += long_fraction < previous ? z.carry : z.no_carry;
                intensity.current += long_fraction < previous ? intensity.carry : intensity.no_carry;
            }
        }
    }
}
