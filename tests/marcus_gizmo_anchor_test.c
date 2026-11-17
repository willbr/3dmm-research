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
    puts("Gizmo display clamping and unrestricted off-screen movement checks passed.");
    return 0;
}
