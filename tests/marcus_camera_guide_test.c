#include "camera_guide.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            fprintf(stderr, "line %d: %s\n", __LINE__, #value);                                                        \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static MarcusProjection projection(void)
{
    MarcusProjection result;
    memset(&result, 0, sizeof(result));
    result.valid = 1;
    result.transform[0] = result.transform[4] = result.transform[8] = 1;
    result.vertical_fov = 90;
    result.aspect = 1;
    result.near_clip = 1;
    result.far_clip = 100;
    result.display_depth = 10;
    return result;
}
static int bounded(const MarcusCameraGuide *guide)
{
    int i;
    const MarcusGuideLine *line;
    for (i = 0; i < guide->line_count; ++i)
    {
        line = &guide->lines[i];
        if (!(line->x1 >= 0 && line->x1 <= 1 && line->y1 >= 0 && line->y1 <= 1 && line->x2 >= 0 && line->x2 <= 1 &&
              line->y2 >= 0 && line->y2 <= 1))
            return 0;
    }
    return guide->line_count <= MARCUS_GUIDE_MAX_LINES;
}
int main(void)
{
    MarcusProjection view, camera, changed;
    MarcusCameraGuide guide;
    int i, edge;
    view = projection();
    camera = projection();
    camera.transform[11] = -5;
    camera.vertical_fov = 45;
    camera.near_clip = 0.1f;
    camera.display_depth = 5;
    camera_guide_build(&view, &camera, &guide);
    CHECK(guide.line_count == 14 && guide.has_label && bounded(&guide));
    CHECK(fabs(guide.label_x - 0.5) < 0.0001 && fabs(guide.label_y - 0.5) < 0.0001);
    changed = camera;
    changed.transform[9] = 2;
    camera_guide_build(&view, &changed, &guide);
    CHECK(guide.has_label && fabs(guide.label_x - 0.7) < 0.0001);
    view.aspect = 2;
    camera_guide_build(&view, &changed, &guide);
    CHECK(guide.has_label && fabs(guide.label_x - 0.6) < 0.0001);
    view = projection();
    view.transform[0] = view.transform[4] = 0;
    view.transform[1] = 1;
    view.transform[3] = -1;
    camera_guide_build(&view, &changed, &guide);
    CHECK(guide.has_label && fabs(guide.label_x - 0.5) < 0.0001 && fabs(guide.label_y - 0.7) < 0.0001);
    view = projection();
    changed = camera;
    changed.transform[11] = 5;
    changed.display_depth = 1;
    camera_guide_build(&view, &changed, &guide);
    CHECK(!guide.line_count && !guide.has_label);
    changed = camera;
    changed.transform[11] = 0;
    camera_guide_build(&view, &changed, &guide);
    CHECK(guide.line_count > 0 && bounded(&guide));
    changed = camera;
    changed.transform[9] = -6;
    camera_guide_build(&view, &changed, &guide);
    CHECK(guide.line_count > 0 && bounded(&guide));
    edge = 0;
    for (i = 0; i < guide.line_count; ++i)
        if (guide.lines[i].x1 < 0.0001 || guide.lines[i].x2 < 0.0001)
            edge = 1;
    CHECK(edge);
    changed.transform[9] = 100;
    camera_guide_build(&view, &changed, &guide);
    CHECK(!guide.line_count && !guide.has_label);
    changed = camera;
    changed.transform[11] = -110;
    camera_guide_build(&view, &changed, &guide);
    CHECK(!guide.line_count);
    changed = view;
    changed.transform[0] = 0;
    camera_guide_build(&changed, &camera, &guide);
    CHECK(!guide.line_count);
    changed = camera;
    changed.vertical_fov = 0;
    camera_guide_build(&view, &changed, &guide);
    CHECK(!guide.line_count);
    changed = camera;
    changed.valid = 0;
    camera_guide_build(&view, &changed, &guide);
    CHECK(!guide.line_count);
    camera_guide_build(&view, NULL, &guide);
    CHECK(!guide.line_count);
    puts("Marcus camera guide: projection, roll, aspect, near/far/side clipping and invalid cameras passed.");
    return 0;
}
