#include "mm/caption_render.h"
#include <float.h>
#include <math.h>

static MMI32 min_value(MMI32 a, MMI32 b) { return a < b ? a : b; }
static MMI32 max_value(MMI32 a, MMI32 b) { return a > b ? a : b; }
static int finite_value(double value) { return value >= -DBL_MAX && value <= DBL_MAX; }

void mm_caption_observe(MMCaptionStack *stack, MMCaptionRow *rows, size_t count, MMI32 frame)
{
    MMCaptionRow *row;
    size_t i;
    int visible;
    for (i = 0; i < count; ++i)
    {
        row = &rows[i];
        visible = mm_text_box_visible_range(row->first, row->end, frame);
        if (visible == row->visible)
            continue;
        if (visible)
        {
            row->previous = stack->last;
            if (stack->last)
                rows[stack->last - 1].next = i + 1;
            else
                stack->first = i + 1;
            stack->last = i + 1;
        }
        else
        {
            if (row->previous)
                rows[row->previous - 1].next = row->next;
            else
                stack->first = row->next;
            if (row->next)
                rows[row->next - 1].previous = row->previous;
            else
                stack->last = row->previous;
            row->next = row->previous = 0;
        }
        row->visible = visible;
    }
}

int mm_caption_hit(const MMCaptionStack *stack, const MMCaptionRow *rows, MMI32 x, MMI32 y, const MMU32 *hidden,
                   MMU32 *chunk)
{
    const MMCaptionRow *row;
    size_t i;
    for (i = stack->last; i; i = row->previous)
    {
        row = &rows[i - 1];
        if (hidden && row->chunk == *hidden)
            continue;
        if (x >= row->bounds.left && x < row->bounds.right && y >= row->bounds.top && y < row->bounds.bottom)
        {
            *chunk = row->chunk;
            return 1;
        }
    }
    return 0;
}

int mm_caption_draw_stack(const MMCaptionStack *stack, const MMCaptionRow *rows, const MMU32 *hidden,
                          int have_draft, MMI32 draft_scroll, void *context,
                          int (*draw)(void *, size_t row, MMI32 scroll))
{
    const MMCaptionRow *row;
    size_t i;
    for (i = stack->first; i; i = row->next)
    {
        row = &rows[i - 1];
        if (hidden && row->chunk == *hidden)
        {
            if (have_draft && !draw(context, 0, draft_scroll))
                return 0;
            have_draft = 0;
        }
        else if (!draw(context, i, row->scroll))
            return 0;
    }
    return !have_draft || draw(context, 0, draft_scroll);
}

MMI32 mm_caption_scroll_overflow(MMI32 height, MMI32 top, MMI32 bottom)
{
    double overflow;
    overflow = (double)height - ((double)bottom - top - 10);
    return overflow <= 0 ? 0 : overflow >= 2147483647.0 ? 2147483647 : (MMI32)overflow;
}

int mm_caption_measure_scroll(MMCaptionRow *rows, size_t count, MMI32 frame, int *needs_scroll,
                              void *context, int (*layout)(void *, size_t row, MMI32 *height))
{
    size_t i;
    MMCaptionRow *row;
    MMI32 height;
    int needs = 0;
    for (i = 0; i < count; ++i)
    {
        row = &rows[i];
        if (mm_text_box_scroll_frame(row->story, row->first, frame))
        {
            if (!layout(context, i + 1, &height))
                return 0;
            row->overflow = mm_caption_scroll_overflow(height, row->bounds.top, row->bounds.bottom);
            needs |= row->scroll < row->overflow;
        }
    }
    *needs_scroll = needs;
    return 1;
}

void mm_caption_step_scroll(MMCaptionRow *rows, size_t count, MMI32 frame)
{
    size_t i;
    MMCaptionRow *row;
    for (i = 0; i < count; ++i)
    {
        row = &rows[i];
        if (mm_text_box_scroll_frame(row->story, row->first, frame) && row->scroll < row->overflow)
            ++row->scroll;
    }
}

void mm_caption_reset_scroll(MMCaptionRow *rows, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i)
        rows[i].scroll = 0;
}

void mm_caption_caret_offsets(const unsigned short *text, MMI32 length, MMI32 *offsets)
{
    MMI32 i, offset = 0;
    unsigned short unit;
    for (i = 0; i < length; ++i)
    {
        unit = text[i];
        offsets[i] = offset;
        if (unit == '\r' && i + 1 < length && text[i + 1] == '\n')
            offsets[++i] = -1;
        if (unit >= 0xd800 && unit <= 0xdbff && i + 1 < length &&
            text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff)
        {
            offsets[++i] = -1;
            offset += 4;
        }
        else
            offset += unit < 0x80 ? 1 : unit < 0x800 ? 2 : 3;
    }
    offsets[length] = offset;
}

int mm_caption_caret_run(MMCaptionCaretBuilder *builder, const MMCaptionLayoutRun *run,
                         const MMCaptionCaretServices *services)
{
    MMCaptionCaretStop stop;
    MMI32 i, span, advance, end = min_value(run->end, builder->length);
    double x;
    int status, replace;
    stop.y = (float)builder->top + MM_CAPTION_BORDER + (float)run->top;
    stop.height = (float)run->height;
    for (i = run->first; i <= end; ++i)
    {
        stop.offset = builder->offsets[i];
        if (stop.offset < 0)
            continue;
        span = i - run->first;
        x = run->x;
        if (span && span < run->drawn)
        {
            status = services->advance(services->context, run->text, span, &advance);
            if (status < 0)
                return status;
            x += advance;
        }
        else if (span)
            x = run->right;
        stop.x = (float)builder->left + MM_CAPTION_INDENT + (float)x;
        replace = builder->last_offset == stop.offset && builder->last_y == stop.y;
        status = services->stop(services->context, &stop, replace);
        if (status < 0)
            return status;
        builder->last_offset = stop.offset;
        builder->last_y = stop.y;
    }
    return 0;
}

static MMI32 caret_scroll_bound(double value)
{
    if (!(value > 0.0))
        return 0;
    return value >= 2147483647.0 ? 2147483647L : (MMI32)value;
}

MMI32 mm_caption_scroll_to_caret(MMI32 scroll, MMI32 layout_height, MMI32 top, MMI32 bottom,
                                float y, float height)
{
    float first = y - (float)top - MM_CAPTION_BORDER;
    float last = first + height - (float)((double)bottom - top - 2 * MM_CAPTION_BORDER);
    scroll = min_value(max_value(scroll, caret_scroll_bound(last)), caret_scroll_bound(first));
    return min_value(scroll, mm_caption_scroll_overflow(layout_height, top, bottom));
}

static int rgba_size(size_t bytes, MMU32 width, MMU32 height)
{
    return width && height && height <= (size_t)-1 / 4 / width && bytes == (size_t)width * height * 4;
}

void mm_caption_layer_size(MMU32 width, MMU32 height, MMU32 *w, MMU32 *h)
{
    /* Every value above the viewport limit produces the same 4096 texels. */
    width = width < 4096 ? width : 4096;
    height = height < 4096 ? height : 4096;
    *w = (MMU32)min_value(4096, max_value(MM_CAPTION_WIDTH,
                            max_value((MMI32)width * 2, (MMI32)height * 2 * MM_CAPTION_WIDTH / MM_CAPTION_HEIGHT)));
    *h = *w * MM_CAPTION_HEIGHT / MM_CAPTION_WIDTH;
}

int mm_caption_viewport(MMPixelRect *clip, MMU32 width, MMU32 height, float aspect)
{
    float w, h;
    MMI32 fitted_w, fitted_h;
    if (!width || !height || width > 4096 || height > 4096 ||
        !(aspect > 0 && aspect <= FLT_MAX))
        return 0;
    w = height * aspect;
    h = width / aspect;
    fitted_w = w < width ? (MMI32)w : (MMI32)width;
    fitted_h = h < height ? (MMI32)h : (MMI32)height;
    if (!fitted_w || !fitted_h)
        return 0;
    clip->left = ((MMI32)width - fitted_w) / 2;
    clip->top = ((MMI32)height - fitted_h) / 2;
    clip->right = clip->left + fitted_w;
    clip->bottom = clip->top + fitted_h;
    return 1;
}

int mm_caption_canvas(MMCaptionCanvas *canvas, unsigned char *rgba, size_t bytes, MMU32 width, MMU32 height,
                      float aspect)
{
    if (!rgba || !rgba_size(bytes, width, height) || !mm_caption_viewport(&canvas->clip, width, height, aspect))
        return 0;
    canvas->rgba = rgba;
    canvas->stride = (size_t)width * 4;
    canvas->scale_x = (float)(canvas->clip.right - canvas->clip.left) / MM_CAPTION_WIDTH;
    canvas->scale_y = (float)(canvas->clip.bottom - canvas->clip.top) / MM_CAPTION_HEIGHT;
    return 1;
}

int mm_caption_box_visible(const MMTextBoxBounds *box)
{
    return box->right > 0 && box->left < MM_CAPTION_WIDTH && box->bottom > 0 && box->top < MM_CAPTION_HEIGHT;
}

static MMI32 pixel_coordinate(MMI32 origin, float value, float scale)
{
    double delta;
    delta = value * scale;
    if (!(delta >= -2147483648.0))
        return (-2147483647 - 1);
    if (delta > 2147483647.0 - origin)
        return 2147483647;
    return origin + (MMI32)delta;
}

static MMPixelRect inset(const MMCaptionCanvas *canvas, const MMTextBoxBounds *box)
{
    MMPixelRect clip;
    clip.left = pixel_coordinate(canvas->clip.left, (float)((double)box->left + MM_CAPTION_BORDER), canvas->scale_x);
    clip.top = pixel_coordinate(canvas->clip.top, (float)((double)box->top + MM_CAPTION_BORDER), canvas->scale_y);
    clip.right = pixel_coordinate(canvas->clip.left, (float)((double)box->right - MM_CAPTION_BORDER), canvas->scale_x);
    clip.bottom = pixel_coordinate(canvas->clip.top, (float)((double)box->bottom - MM_CAPTION_BORDER), canvas->scale_y);
    clip.left = max_value(clip.left, canvas->clip.left);
    clip.top = max_value(clip.top, canvas->clip.top);
    clip.right = min_value(clip.right, canvas->clip.right);
    clip.bottom = min_value(clip.bottom, canvas->clip.bottom);
    return clip;
}

static void over(unsigned char *target, const unsigned char *rgb, unsigned alpha, int invert)
{
    unsigned old, coverage, keep, value;
    int channel;
    if (!alpha)
        return;
    old = target[3];
    keep = old * (255U - alpha);
    coverage = alpha * 255U + keep;
    for (channel = 0; channel < 3; ++channel)
    {
        value = invert ? 255U - target[channel] : rgb[channel];
        target[channel] = (unsigned char)((target[channel] * keep + value * alpha * 255U) / coverage);
    }
    target[3] = (unsigned char)(coverage / 255U);
}

static void paint(unsigned char *pixel, MMU32 colour, unsigned alpha)
{
    unsigned char rgb[3];
    if (colour == 0xffffffffUL || !alpha)
        return;
    rgb[0] = (unsigned char)(colour >> 16);
    rgb[1] = (unsigned char)(colour >> 8);
    rgb[2] = (unsigned char)colour;
    over(pixel, rgb, alpha, colour == 0xff000000UL);
}

void mm_caption_box(MMCaptionCanvas *canvas, const MMTextBoxBounds *box, MMU32 colour)
{
    MMPixelRect clip;
    MMI32 x, y;
    unsigned char *row;
    if (colour == 0xffffffffUL)
        return;
    clip = inset(canvas, box);
    for (y = clip.top; y < clip.bottom; ++y)
    {
        row = canvas->rgba + (size_t)y * canvas->stride;
        for (x = clip.left; x < clip.right; ++x)
            paint(row + (size_t)x * 4, colour, 255);
    }
}

int mm_caption_raster_scale(const MMCaptionCanvas *canvas, MMI32 pixels)
{
    const MMI32 limit = pixels > 0 ? max_value(1, 1024 / pixels) : 1;
    return min_value(limit, (MMI32)ceil(canvas->scale_x > canvas->scale_y ? canvas->scale_x : canvas->scale_y));
}

static MMI32 mask_coordinate(MMI32 point, MMI32 start, float density, float scale, MMI32 extent)
{
    float coordinate;
    coordinate = (float)((double)point - start) * density / scale;
    return coordinate >= extent - 1 ? extent - 1 : (MMI32)coordinate;
}

void mm_caption_glyph(MMCaptionCanvas *canvas, const MMTextBoxBounds *box, MMI32 scroll, const MMCaptionGlyph *glyph)
{
    const MMCaptionMask *mask;
    MMPixelRect clip;
    float gx, gy, density;
    MMI32 start_x, start_y, finish_x, finish_y, x, y, px, py;
    unsigned char *pixel;
    size_t src;
    mask = &glyph->mask;
    density = glyph->density;
    if (!glyph->ink || mask->width <= 0 || mask->height <= 0 || mask->pitch < mask->width ||
        !(density > 0 && density <= FLT_MAX))
        return;
    gx = glyph->x + mask->left / density + MM_CAPTION_INDENT;
    gy = glyph->y + mask->top / density - scroll + MM_CAPTION_BORDER +
         (glyph->supersampled ? glyph->ascent - glyph->raster_ascent / (density * 2) : 0);
    start_x = pixel_coordinate(canvas->clip.left, box->left + gx, canvas->scale_x);
    start_y = pixel_coordinate(canvas->clip.top, box->top + gy, canvas->scale_y);
    finish_x = pixel_coordinate(canvas->clip.left, box->left + gx + mask->width / density, canvas->scale_x);
    finish_y = pixel_coordinate(canvas->clip.top, box->top + gy + mask->height / density, canvas->scale_y);
    clip = inset(canvas, box);
    for (y = max_value(clip.top, start_y); y < min_value(clip.bottom, finish_y); ++y)
    {
        py = mask_coordinate(y, start_y, density, canvas->scale_y, mask->height);
        for (x = max_value(clip.left, start_x); x < min_value(clip.right, finish_x); ++x)
        {
            px = mask_coordinate(x, start_x, density, canvas->scale_x, mask->width);
            src = (size_t)py * mask->pitch + px;
            pixel = canvas->rgba + (size_t)y * canvas->stride + (size_t)x * 4;
            if (glyph->paper)
                paint(pixel, glyph->background, glyph->paper[src]);
            paint(pixel, glyph->foreground, glyph->ink[src]);
        }
    }
}

void mm_caption_mask_half_size(const MMCaptionMask *from, MMCaptionMask *to)
{
    to->left = (MMI32)floor(from->left / 2.0);
    to->top = (MMI32)floor(from->top / 2.0);
    to->width = (MMI32)ceil(((double)from->left + from->width) / 2.0) - to->left;
    to->height = (MMI32)ceil(((double)from->top + from->height) / 2.0) - to->top;
    to->pitch = to->width;
}

void mm_caption_mask_half(const MMCaptionMask *from, const unsigned char *src, const MMCaptionMask *to,
                         unsigned char *dst)
{
    MMI32 x, y, dx, dy, sx, sy;
    unsigned sum;
    for (y = 0; y < to->height; ++y)
        for (x = 0; x < to->width; ++x)
        {
            sum = 0;
            for (dy = 0; dy < 2; ++dy)
                for (dx = 0; dx < 2; ++dx)
                {
                    sx = (to->left + x) * 2 + dx - from->left;
                    sy = (to->top + y) * 2 + dy - from->top;
                    if (sx >= 0 && sy >= 0 && sx < from->width && sy < from->height)
                        sum += src[(size_t)sy * from->pitch + sx];
                }
            dst[(size_t)y * to->pitch + x] = (unsigned char)((sum + 2) / 4);
        }
}

void mm_caption_mask_expand(const MMCaptionMask *from, const unsigned char *src, const MMCaptionMask *to,
                            unsigned char *dst, int scale)
{
    MMI32 x, y;
    for (y = 0; y < to->height; ++y)
        for (x = 0; x < to->width; ++x)
            dst[(size_t)y * to->pitch + x] = src[(size_t)(y / scale) * from->pitch + x / scale];
}

static int projection_valid(const MMPreviewProjection *p)
{
    size_t i;
    if (!p->valid || !(p->vertical_fov > 0 && p->vertical_fov < 179) ||
        !(p->aspect > 0 && p->aspect <= FLT_MAX) || !(p->near_clip > 0 && p->far_clip > p->near_clip &&
        p->far_clip <= FLT_MAX) || (p->orthographic && !(p->ortho_height > 0 && p->ortho_height <= FLT_MAX)))
        return 0;
    for (i = 0; i < 12; ++i)
        if (!finite_value(p->transform[i]))
            return 0;
    return 1;
}

void mm_caption_quad(const unsigned char *texture, size_t texture_bytes, MMU32 tw, MMU32 th,
                     const MMPreviewProjection *camera, const MMPreviewProjection *view,
                     unsigned char *rgba, size_t rgba_bytes, MMU32 width, MMU32 height,
                     const float *depth, size_t depth_count, MMU32 *covered)
{
    const float *matrix;
    double inverse[3][3], origin[3] = {0}, basis[3][3] = {{0}};
    double determinant, half_h, half_w, slope_x, slope_y, ray_x, ray_y, offset, distance, u, v;
    double ray_origin[3], direction[3];
    const unsigned char *source;
    unsigned char *target;
    MMU32 x, y, tx, ty;
    size_t pixel;
    int row, axis, d, has_depth;
    if (!texture || !rgba || !projection_valid(camera) || !projection_valid(view) ||
        !(camera->display_depth > 0 && camera->display_depth <= FLT_MAX) ||
        !rgba_size(texture_bytes, tw, th) || !rgba_size(rgba_bytes, width, height))
        return;
    matrix = camera->transform;
    for (row = 0; row < 3; ++row)
        for (axis = 0; axis < 3; ++axis)
            inverse[row][axis] =
                matrix[((row + 1) % 3) * 3 + (axis + 1) % 3] * (double)matrix[((row + 2) % 3) * 3 + (axis + 2) % 3] -
                matrix[((row + 1) % 3) * 3 + (axis + 2) % 3] * (double)matrix[((row + 2) % 3) * 3 + (axis + 1) % 3];
    determinant = matrix[0] * inverse[0][0] + matrix[1] * inverse[0][1] + matrix[2] * inverse[0][2];
    if (!finite_value(determinant) || fabs(determinant) < 1e-12)
        return;
    for (row = 0; row < 3; ++row)
        for (axis = 0; axis < 3; ++axis)
        {
            inverse[row][axis] /= determinant;
            origin[row] += inverse[row][axis] * (view->transform[9 + axis] - matrix[9 + axis]);
            for (d = 0; d < 3; ++d)
                basis[row][d] += inverse[row][axis] * view->transform[d * 3 + axis];
        }
    half_h = tan(camera->vertical_fov * .008726646259971648) * camera->display_depth;
    half_w = half_h * camera->aspect;
    slope_y = view->orthographic ? view->ortho_height * .5 : tan(view->vertical_fov * .008726646259971648);
    slope_x = slope_y * view->aspect;
    has_depth = depth && depth_count == (size_t)width * height;
    for (y = 0; y < height; ++y)
    {
        ray_y = (1 - 2 * (y + .5) / height) * slope_y;
        for (x = 0; x < width; ++x)
        {
            ray_x = (2 * (x + .5) / width - 1) * slope_x;
            for (axis = 0; axis < 3; ++axis)
            {
                offset = basis[axis][0] * ray_x + basis[axis][1] * ray_y;
                ray_origin[axis] = origin[axis] + (view->orthographic ? offset : 0);
                direction[axis] = -basis[axis][2] + (view->orthographic ? 0 : offset);
            }
            if (fabs(direction[2]) < 1e-12)
                continue;
            distance = (-camera->display_depth - ray_origin[2]) / direction[2];
            if (!(distance >= view->near_clip && distance <= view->far_clip))
                continue;
            pixel = (size_t)y * width + x;
            if (has_depth && distance > depth[pixel] + .001)
                continue;
            u = .5 + (ray_origin[0] + direction[0] * distance) / (2 * half_w);
            v = .5 - (ray_origin[1] + direction[1] * distance) / (2 * half_h);
            if (!(u >= 0 && u < 1 && v >= 0 && v < 1))
                continue;
            tx = (MMU32)(u * tw);
            ty = (MMU32)(v * th);
            source = texture + ((size_t)ty * tw + tx) * 4;
            if (!source[3])
                continue;
            target = rgba + pixel * 4;
            if (!target[3])
                ++*covered;
            over(target, source, source[3], 0);
        }
    }
}
