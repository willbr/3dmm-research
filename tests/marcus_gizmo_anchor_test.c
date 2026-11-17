/* Headless projection and constrained-drag math; no UI or simulated input. */
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
int main(void)
{
    MarcusProjection camera;
    float world[3] = {100, -100, -5}, original[3], anchor[3], start[3] = {0, 0, -5};
    float proposed[3] = {100, 0, -5}, clipped[3], offset[3], image[3];
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
    proposed[0] = anchor[0] - 1; proposed[1] = anchor[1]; proposed[2] = anchor[2];
    check(camera_gizmo_clip(&camera, anchor, proposed, .1f, .1f, clipped), "Drag the offset display anchor");
    check(near(clipped[0] - offset[0], world[0] - 1) && near(clipped[1] - offset[1], world[1]) &&
        near(clipped[2] - offset[2], world[2]), "Proxy applies only the drag delta to the object");
    proposed[0] = 100; proposed[1] = 0; proposed[2] = -5;
    check(camera_gizmo_clip(&camera, start, proposed, .1f, .1f, clipped), "Clip an axis drag");
    check(near(clipped[0], 4) && near(clipped[1], 0) && near(clipped[2], -5), "Axis drag stops at the edge without changing other axes");
    visible(&camera, clipped);
    proposed[0] = 3;
    check(camera_gizmo_clip(&camera, clipped, proposed, .1f, .1f, anchor) && near(anchor[0], 3), "Dragging inward resumes at the edge");
    proposed[0] = 100; proposed[1] = 50;
    check(camera_gizmo_clip(&camera, start, proposed, .1f, .1f, clipped), "Clip a plane drag");
    check(near(clipped[0], 4) && near(clipped[1], 2), "Plane drag retains its original world-space direction");
    proposed[0] = proposed[1] = 0; proposed[2] = 100;
    check(camera_gizmo_clip(&camera, start, proposed, .1f, .1f, clipped), "Clip a drag toward the camera");
    visible(&camera, clipped);
    check(clipped[2] < -.2f, "Perspective anchor stays past ImGuizmo's camera guard");
    world[0] = 100; world[1] = -100; world[2] = 100;
    check(camera_gizmo_anchor(&camera, world, .1f, .1f, anchor), "Recover an origin behind the camera");
    visible(&camera, anchor);
    check(camera_gizmo_anchor(&camera, start, .1f, .1f, anchor) && !memcmp(start, anchor, sizeof(start)),
        "Already visible origins remain bit-for-bit unchanged");
    camera.transform[0] = camera.transform[4] = 0; camera.transform[1] = 1; camera.transform[3] = -1;
    check(camera_gizmo_anchor(&camera, world, .1f, .1f, anchor), "Clamp using a rolled camera");
    visible(&camera, anchor);
    camera.orthographic = 1; camera.ortho_height = 10;
    world[2] = -50;
    check(camera_gizmo_anchor(&camera, world, .1f, .1f, anchor), "Clamp orthographic origin");
    visible(&camera, anchor);
    check(near(anchor[2], -50), "Orthographic clamping retains depth");
    camera.aspect = 2;
    check(camera_gizmo_anchor(&camera, world, .1f, .1f, anchor) && camera_project_point(&camera, anchor, image), "Use image aspect for wide viewports");
    visible(&camera, anchor);
    check(!camera_gizmo_anchor(&camera, world, .5f, .1f, anchor), "Reject invalid insets");
    camera.valid = 0;
    check(!camera_gizmo_anchor(&camera, world, .1f, .1f, anchor), "Reject missing projections");
    puts("Gizmo anchor projection, viewport clipping and constrained drag math passed.");
    return 0;
}
