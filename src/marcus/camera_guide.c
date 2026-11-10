#include "camera_guide.h"
#include <float.h>
#include <math.h>
#include <string.h>

typedef struct GuideView
{
    double inverse[3][3], origin[3], tangent_x, tangent_y, near_clip, far_clip;
    int orthographic;
} GuideView;

static int finite_value(double value)
{
    return value == value && fabs(value) <= DBL_MAX;
}
static int valid_projection(const MarcusProjection *projection)
{
    int i;
    if (!projection || !projection->valid || !finite_value(projection->vertical_fov) || projection->vertical_fov <= 0 ||
        projection->vertical_fov >= 179 || !finite_value(projection->aspect) || projection->aspect <= 0 ||
        !finite_value(projection->near_clip) || projection->near_clip <= 0 || !finite_value(projection->far_clip) ||
        projection->far_clip <= projection->near_clip)
        return 0;
    if (projection->orthographic && (!finite_value(projection->ortho_height) || projection->ortho_height <= 0))
        return 0;
    for (i = 0; i < 12; ++i)
        if (!finite_value(projection->transform[i]))
            return 0;
    return 1;
}
static void world_point(const MarcusProjection *camera, double x, double y, double z, double point[3])
{
    int axis;
    for (axis = 0; axis < 3; ++axis)
        point[axis] = camera->transform[9 + axis] + camera->transform[axis] * x + camera->transform[3 + axis] * y -
                      camera->transform[6 + axis] * z;
}
static int make_view(const MarcusProjection *projection, GuideView *view)
{
    const float *matrix;
    double determinant;
    int row, axis;
    matrix = projection->transform;
    for (row = 0; row < 3; ++row)
        for (axis = 0; axis < 3; ++axis)
            view->inverse[row][axis] =
                matrix[((row + 1) % 3) * 3 + (axis + 1) % 3] * (double)matrix[((row + 2) % 3) * 3 + (axis + 2) % 3] -
                matrix[((row + 1) % 3) * 3 + (axis + 2) % 3] * (double)matrix[((row + 2) % 3) * 3 + (axis + 1) % 3];
    determinant = matrix[0] * view->inverse[0][0] + matrix[1] * view->inverse[0][1] + matrix[2] * view->inverse[0][2];
    if (fabs(determinant) < 1e-12)
        return 0;
    for (row = 0; row < 3; ++row)
        for (axis = 0; axis < 3; ++axis)
            view->inverse[row][axis] /= determinant;
    for (axis = 0; axis < 3; ++axis)
        view->origin[axis] = matrix[9 + axis];
    view->tangent_y = tan(projection->vertical_fov * 0.008726646259971648);
    view->orthographic = projection->orthographic;
    if (view->orthographic) view->tangent_y = projection->ortho_height * .5;
    view->tangent_x = view->tangent_y * projection->aspect;
    view->near_clip = projection->near_clip;
    view->far_clip = projection->far_clip;
    return 1;
}
static void view_point(const GuideView *view, const double world[3], double point[3])
{
    int row, axis;
    for (row = 0; row < 3; ++row)
    {
        point[row] = 0;
        for (axis = 0; axis < 3; ++axis)
            point[row] += (world[axis] - view->origin[axis]) * view->inverse[row][axis];
    }
    point[2] = -point[2];
}
static double plane_distance(const GuideView *view, const double point[3], int plane)
{
    double factor;
    factor = view->orthographic ? 1 : point[2];
    switch (plane)
    {
    case 0:
        return point[2] - view->near_clip;
    case 1:
        return view->far_clip - point[2];
    case 2:
        return factor * view->tangent_x + point[0];
    case 3:
        return factor * view->tangent_x - point[0];
    case 4:
        return factor * view->tangent_y + point[1];
    default:
        return factor * view->tangent_y - point[1];
    }
}
static float coordinate(double value)
{
    if (value < 0)
        return 0;
    if (value > 1)
        return 1;
    return (float)value;
}
static void add_line(const GuideView *view, const double world_a[3], const double world_b[3], MarcusCameraGuide *guide)
{
    double a[3], b[3], start[3], end[3], low, high, fa, fb, crossing;
    int plane, axis;
    MarcusGuideLine *line;
    view_point(view, world_a, a);
    view_point(view, world_b, b);
    low = 0;
    high = 1;
    for (plane = 0; plane < 6; ++plane)
    {
        fa = plane_distance(view, a, plane);
        fb = plane_distance(view, b, plane);
        if (fa < 0 && fb < 0)
            return;
        if ((fa < 0) != (fb < 0))
        {
            crossing = fa / (fa - fb);
            if (fa < 0)
            {
                if (crossing > low)
                    low = crossing;
            }
            else if (crossing < high)
                high = crossing;
        }
        if (low > high)
            return;
    }
    if (guide->line_count >= MARCUS_GUIDE_MAX_LINES)
        return;
    for (axis = 0; axis < 3; ++axis)
    {
        start[axis] = a[axis] + (b[axis] - a[axis]) * low;
        end[axis] = a[axis] + (b[axis] - a[axis]) * high;
    }
    line = &guide->lines[guide->line_count++];
    line->x1 = coordinate(0.5 + start[0] / (2 * (view->orthographic ? 1 : start[2]) * view->tangent_x));
    line->y1 = coordinate(0.5 - start[1] / (2 * (view->orthographic ? 1 : start[2]) * view->tangent_y));
    line->x2 = coordinate(0.5 + end[0] / (2 * (view->orthographic ? 1 : end[2]) * view->tangent_x));
    line->y2 = coordinate(0.5 - end[1] / (2 * (view->orthographic ? 1 : end[2]) * view->tangent_y));
}
void camera_guide_build(const MarcusProjection *projection, const MarcusProjection *camera, MarcusCameraGuide *guide)
{
    GuideView view;
    double points[4][3], origin[3], top[3][3], point[3], depth, half_height, half_width;
    int i, corner, next, plane, inside;
    memset(guide, 0, sizeof(*guide));
    if (!valid_projection(projection) || !valid_projection(camera) || !finite_value(camera->display_depth) ||
        camera->display_depth <= 0 || !make_view(projection, &view))
        return;
    depth = camera->display_depth;
    half_height = tan(camera->vertical_fov * 0.008726646259971648) * depth;
    half_width = half_height * camera->aspect;
    for (corner = 0; corner < 4; ++corner)
        world_point(camera, (corner == 1 || corner == 2) ? half_width : -half_width,
                    corner >= 2 ? half_height : -half_height, depth, points[corner]);
    world_point(camera, 0, 0, 0, origin);
    for (corner = 0; corner < 4; ++corner)
    {
        next = (corner + 1) % 4;
        add_line(&view, points[corner], points[next], guide);
        add_line(&view, origin, points[corner], guide);
    }
    /* An upright marker makes camera roll and the top of its image clear. */
    world_point(camera, -half_width * 0.25, half_height, depth, top[0]);
    world_point(camera, 0, half_height * 1.3, depth, top[1]);
    world_point(camera, half_width * 0.25, half_height, depth, top[2]);
    add_line(&view, top[0], top[1], guide);
    add_line(&view, top[1], top[2], guide);
    for (i = 0; i < 2 && !guide->has_label; ++i)
    {
        if (i)
            world_point(camera, 0, 0, depth, origin);
        view_point(&view, origin, point);
        inside = 1;
        for (plane = 0; plane < 6; ++plane)
            if (plane_distance(&view, point, plane) < 0)
                inside = 0;
        if (inside)
        {
            guide->has_label = 1;
            guide->label_x = coordinate(0.5 + point[0] / (2 * (view.orthographic ? 1 : point[2]) * view.tangent_x));
            guide->label_y = coordinate(0.5 - point[1] / (2 * (view.orthographic ? 1 : point[2]) * view.tangent_y));
        }
    }
}
void object_bounds_build(const MarcusImage *image, const MarcusBounds *bounds, MarcusCameraGuide *guide)
{
    GuideView view;
    double a[3], b[3], width, height, left, top;
    int corner, axis, coordinate_index, next, i;
    MarcusGuideLine *line;
    memset(guide, 0, sizeof(*guide));
    if (!image || !bounds || !bounds->valid || image->width <= 0 || image->height <= 0 ||
        !valid_projection(&image->projection) || !make_view(&image->projection, &view))
        return;
    for (i = 0; i < 24; ++i)
        if (!finite_value(bounds->corners[i]))
            return;
    for (corner = 0; corner < 8; ++corner)
        for (axis = 0; axis < 3; ++axis)
            if (!(corner & (1 << axis)))
            {
                next = corner | (1 << axis);
                for (coordinate_index = 0; coordinate_index < 3; ++coordinate_index)
                {
                    a[coordinate_index] = bounds->corners[corner * 3 + coordinate_index];
                    b[coordinate_index] = bounds->corners[next * 3 + coordinate_index];
                }
                add_line(&view, a, b, guide);
            }
    if (!image->authored_camera)
        return;
    /* The movie's projection covers its source image, which is fitted inside
     * the output. Inspection projections already cover the whole output. */
    width = image->width;
    height = width / image->projection.aspect;
    if (height > image->height)
    {
        height = image->height;
        width = height * image->projection.aspect;
    }
    left = (image->width - width) / 2;
    top = (image->height - height) / 2;
    for (i = 0; i < guide->line_count; ++i)
    {
        line = &guide->lines[i];
        line->x1 = (float)((left + line->x1 * width) / image->width);
        line->y1 = (float)((top + line->y1 * height) / image->height);
        line->x2 = (float)((left + line->x2 * width) / image->width);
        line->y2 = (float)((top + line->y2 * height) / image->height);
    }
}
