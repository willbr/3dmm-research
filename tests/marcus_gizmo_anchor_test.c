/* Headless display projection and off-screen drag math; no UI or simulated input. */
#include "camera_guide.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, const char *message)
{
    if (!ok) { fprintf(stderr, "%s\n", message); exit(1); }
}
static int near(float a, float b) { return fabs(a - b) < .0001; }
static void visible(const MarcusProjection *camera, const float world[3])
{
    float image[3];
    check(camera_project_point(camera, world, image), "Anchor remains visible");
    check(image[0] >= .0999f && image[0] <= .9001f && image[1] >= .0999f && image[1] <= .9001f,
        "Anchor retains the viewport inset");
}
static void displayed(const MarcusProjection *camera, const float world[3])
{
    float view[16], projection[16], shift[2], point[4], clip[4], screen[2];
    int r, c;
    check(camera_projection_matrices(camera, view, projection), "Read projection matrices");
    check(camera_gizmo_display_shift(camera, world, .1f, .1f, shift), "Compute display-only clamp");
    for (r = 0; r < 4; ++r)
    {
        point[r] = view[12 + r];
        for (c = 0; c < 3; ++c) point[r] += view[c * 4 + r] * world[c];
    }
    for (r = 0; r < 4; ++r)
    {
        clip[r] = 0;
        for (c = 0; c < 4; ++c) clip[r] += projection[c * 4 + r] * point[c];
    }
    screen[0] = .5f + clip[0] / (2 * clip[3]) + shift[0];
    screen[1] = .5f - clip[1] / (2 * clip[3]) + shift[1];
    check(screen[0] >= .0999f && screen[0] <= .9001f && screen[1] >= .0999f && screen[1] <= .9001f,
        "Drawing stays inside the viewport while the real position is off-screen");
}
static void ground_grid(void)
{
    static unsigned char rgba[400 * 400 * 4];
    static float depth[400 * 400];
    MarcusImage image;
    MarcusGuideLine lines[MARCUS_GROUND_GRID_MAX_LINES];
    int i, count, drawn;
    memset(&image, 0, sizeof(image));
    image.width = image.height = 400;
    image.authored_camera = 1;
    image.projection.valid = 1;
    image.projection.aspect = 2;
    image.projection.vertical_fov = 90;
    image.projection.near_clip = 25;
    image.projection.far_clip = 100;
    image.projection.transform[0] = 1;
    image.projection.transform[5] = -1;
    image.projection.transform[7] = 1;
    image.projection.transform[10] = 20;
    count = camera_ground_grid(&image, lines);
    check(count > 0 && count <= MARCUS_GROUND_GRID_MAX_LINES, "Top-down grid extends in front of the fitted near plane");
    for (i = 0; i < count; ++i)
        check(lines[i].y1 >= .25f && lines[i].y1 <= .75f && lines[i].y2 >= .25f && lines[i].y2 <= .75f,
              "Grid clips to the authored picture inside letterboxing");
    for (i = 0; i < 400 * 400; ++i) depth[i] = i % 400 < 200 ? 5.f : 40.f;
    camera_blend_ground_grid(&image, rgba, depth);
    drawn = 0;
    for (i = 0; i < 400 * 400; ++i)
        if (rgba[i * 4 + 3])
        {
            check(i % 400 >= 200 && i / 400 >= 100 && i / 400 < 300,
                  "Foreground depth and letterboxing occlude the grid");
            ++drawn;
        }
    check(drawn > 0, "The nearby grid renders over farther surfaces");
    image.projection.orthographic = 1;
    image.projection.ortho_height = 20;
    image.projection.near_clip = 1;
    image.projection.far_clip = 10;
    check(camera_ground_grid(&image, lines) > 0, "Orthographic grid extends past the fitted far plane");
    memset(rgba, 0, sizeof(rgba));
    camera_blend_ground_grid(&image, rgba, depth);
    drawn = 0;
    for (i = 0; i < 400 * 400; ++i)
        if (rgba[i * 4 + 3])
        {
            check(i % 400 >= 200, "The orthographic grid respects foreground depth");
            ++drawn;
        }
    check(drawn > 0, "The orthographic grid renders over farther surfaces");
    image.projection.transform[5] = 1;
    image.projection.transform[7] = -1;
    check(!camera_ground_grid(&image, lines), "Camera looking up sees no ground grid");
}
static void camera_markers(void)
{
    MarcusImage image = {0};
    MarcusProjection camera;
    MarcusCameraGuide guide;
    image.width = 640;
    image.height = 360;
    image.projection.valid = 1;
    image.projection.aspect = 16.f / 9;
    image.projection.vertical_fov = 60;
    image.projection.near_clip = 1;
    image.projection.far_clip = 100;
    image.projection.transform[0] = image.projection.transform[4] = image.projection.transform[8] = 1;
    camera = image.projection;
    camera.vertical_fov = 20;
    camera.transform[11] = -2;
    camera.display_depth = 4;

    image.projection.near_clip = 10;
    camera_guide_build_image(&image, &camera, &guide);
    check(guide.line_count == 10 && guide.has_label, "Camera marker remains visible in front of the fitted near plane");
    image.projection.near_clip = 1;
    image.projection.far_clip = 1.5f;
    camera_guide_build_image(&image, &camera, &guide);
    check(guide.line_count == 10 && guide.has_label, "Camera marker remains visible past the fitted far plane");
    camera.transform[11] = 2;
    camera_guide_build_image(&image, &camera, &guide);
    check(guide.line_count > 0 && guide.has_label, "Visible edges survive when a marker crosses the viewer");
    camera.transform[11] = 6;
    camera_guide_build_image(&image, &camera, &guide);
    check(!guide.line_count && !guide.has_label, "Camera marker wholly behind the viewer is clipped");
}
int main(void)
{
    MarcusProjection camera;
    float world[3] = {100, -100, -5}, original[3], anchor[3], start[3] = {0, 0, -5};
    float proposed[3], moved[3], offset[3], image[3], shift[2];
    int i;
    memset(&camera, 0, sizeof(camera));
    camera.valid = 1; camera.vertical_fov = 90; camera.aspect = 1;
    camera.near_clip = .1f; camera.far_clip = 100;
    camera.transform[0] = camera.transform[4] = camera.transform[8] = 1;
    memcpy(original, world, sizeof(world));
    check(camera_gizmo_anchor(&camera, world, .1f, .1f, anchor), "Clamp off-screen origin");
    check(!memcmp(world, original, sizeof(world)), "Clamping leaves the real object untouched");
    visible(&camera, anchor);
    check(near(anchor[0], 4) && near(anchor[1], -4) && near(anchor[2], -5), "Perspective clamp retains camera depth");
    for (i = 0; i < 3; ++i) offset[i] = anchor[i] - world[i];
    proposed[0] = anchor[0] + 100; proposed[1] = anchor[1]; proposed[2] = anchor[2];
    for (i = 0; i < 3; ++i) moved[i] = proposed[i] - offset[i];
    check(near(moved[0], world[0] + 100) && near(moved[1], world[1]) && near(moved[2], world[2]),
        "Actor keeps the full axis drag beyond the viewport boundary");
    displayed(&camera, proposed);
    check(camera_gizmo_display_shift(&camera, proposed, .1f, .1f, shift) && near(moved[0], 200),
        "Clamping the drawing does not clamp the actor position");
    for (i = 0; i < 12; ++i)
    {
        proposed[0] = (float)(i - 6) * 20; proposed[1] = proposed[0] * .5f; proposed[2] = -5;
        memcpy(original, proposed, sizeof(proposed));
        displayed(&camera, proposed);
        check(!memcmp(original, proposed, sizeof(proposed)), "Off-screen plane movement retains its complete world-space delta");
    }
    world[0] = 100; world[1] = -100; world[2] = 100;
    check(camera_gizmo_anchor(&camera, world, .1f, .1f, anchor), "Recover an origin behind the camera");
    visible(&camera, anchor);
    displayed(&camera, world);
    check(camera_gizmo_anchor(&camera, start, .1f, .1f, anchor) && !memcmp(start, anchor, sizeof(start)),
        "Already visible origins remain bit-for-bit unchanged");
    camera.transform[0] = camera.transform[4] = 0; camera.transform[1] = 1; camera.transform[3] = -1;
    check(camera_gizmo_anchor(&camera, world, .1f, .1f, anchor), "Clamp using a rolled camera");
    visible(&camera, anchor);
    displayed(&camera, world);
    camera.orthographic = 1; camera.ortho_height = 10;
    world[2] = -50;
    check(camera_gizmo_anchor(&camera, world, .1f, .1f, anchor), "Clamp orthographic origin");
    visible(&camera, anchor);
    check(near(anchor[2], -50), "Orthographic clamping retains depth");
    displayed(&camera, world);
    camera.aspect = 2;
    check(camera_gizmo_anchor(&camera, world, .1f, .1f, anchor) && camera_project_point(&camera, anchor, image), "Use image aspect for wide viewports");
    visible(&camera, anchor);
    displayed(&camera, world);
    check(!camera_gizmo_anchor(&camera, world, .5f, .1f, anchor), "Reject invalid insets");
    camera.valid = 0;
    check(!camera_gizmo_anchor(&camera, world, .1f, .1f, anchor), "Reject missing projections");
    ground_grid();
    camera_markers();
    puts("Gizmo display clamping and unrestricted off-screen movement checks passed.");
    return 0;
}
