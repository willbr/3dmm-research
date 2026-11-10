#include "player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            fprintf(stderr, "line %d: %s: %s\n", __LINE__, #value, marcus_error(player));                              \
            goto failed;                                                                                               \
        }                                                                                                              \
    } while (0)

int main(int argc, char **argv)
{
    MarcusPlayer *player, *other;
    MarcusMovieInput *input;
    MarcusInfo info;
    MarcusScene scene;
    MarcusObject first, later, prop, inactive;
    MarcusBounds first_bounds, bounds;
    MarcusImage image, pick_image;
    MarcusCamera camera, moved;
    MarcusProjection authored, orbit, manual, flight, panned;
    float guide_depth[2];
    const long frames[] = {1, 5, 10, 16, 25, 30, 1};
    unsigned char *pixels;
    size_t bytes;
    int index, scene_count, picked, pick_x, pick_y, x, y, hits, renderer, expected_pick;
    if (argc != 7)
        return 2;
    player = marcus_create();
    other = NULL;
    input = NULL;
    pixels = NULL;
    CHECK(player);
    CHECK(!marcus_info(player, &info));
    CHECK(!marcus_seek(player, 1));
    CHECK(!marcus_scene(player, -1, &scene));
    CHECK(!marcus_object_bounds(player, 0, &bounds) && !bounds.valid);
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]));
    CHECK(marcus_info(player, &info));
    scene_count = info.scene_count;
    CHECK(info.scene_count == 2 && info.object_count == 4 && info.first_frame == 1 && info.last_frame == 30);
    CHECK(info.sound_count == 0 && info.sound_track_count == 0);
    CHECK(marcus_scene(player, 0, &scene) && scene.first_frame == 1 && scene.last_frame == 30);
    CHECK(marcus_object(player, 0, &first));
    CHECK(!strcmp(first.name, "Animated actor") && first.visible && !first.prop && first.has_lifetime);
    CHECK(marcus_object(player, 1, &prop) && prop.prop);
    CHECK(!marcus_object(player, -1, &later));
    CHECK(!marcus_object(player, 4, &later));
    CHECK(marcus_object_bounds(player, 0, &first_bounds) && first_bounds.valid);
    CHECK(marcus_object_bounds(player, 1, &bounds) && bounds.valid); /* Prop. */
    CHECK(marcus_object_bounds(player, 3, &bounds) && bounds.valid); /* 3D text. */
    CHECK(!marcus_object_bounds(player, -1, &bounds) && !bounds.valid);
    CHECK(!marcus_object_bounds(player, info.object_count, &bounds) && !bounds.valid);
    CHECK(!marcus_object_bounds(player, 0, NULL));
    for (index = 0; index < 24; ++index)
        CHECK(fabs(first_bounds.corners[index]) < 32767);
    CHECK(memcmp(first_bounds.corners, first_bounds.corners + 21, 3 * sizeof(float)));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(image.width == 480 && image.height == 360 && image.pitch == 480 * 4 && image.rgba);
    CHECK(image.projection.valid && image.scene_camera.valid && image.scene_camera.vertical_fov > 0);
    CHECK(image.scene_camera.far_clip > image.scene_camera.near_clip && image.scene_camera.display_depth > 0);
    authored = image.scene_camera;
    guide_depth[0] = authored.display_depth;
    bytes = (size_t)image.pitch * image.height;
    pixels = (unsigned char *)malloc(bytes);
    CHECK(pixels);
    memcpy(pixels, image.rgba, bytes);
    /* Picking uses visible faces in the rendered camera, retains object-list
     * identities, and does not invalidate pixels or alter the evaluated frame. */
    pick_image = image;
    pick_image.rgba = NULL;
    CHECK(marcus_pick(player, &pick_image, -1, 0, &picked) && picked == -1);
    CHECK(marcus_pick(player, &pick_image, image.width, 0, &picked) && picked == -1);
    hits = 0;
    pick_x = pick_y = -1;
    expected_pick = -1;
    for (y = 6; y < image.height; y += 18)
        for (x = 6; x < image.width; x += 18)
        {
            CHECK(marcus_pick(player, &pick_image, x, y, &picked));
            if (picked >= 0)
            {
                CHECK(marcus_object(player, picked, &later) && later.visible);
                CHECK(later.actor_id == picked); /* Fixture IDs follow object-list order. */
                ++hits;
                pick_x = x;
                pick_y = y;
                expected_pick = picked;
            }
        }
    CHECK(hits > 0 && pick_x >= 0);
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_info(player, &info) && info.frame == 1 && info.camera == 0);
    for (renderer = MARCUS_RENDERER_BRENDER_COMPUTE; renderer < MARCUS_RENDERER_COUNT; ++renderer)
    {
        if (!marcus_set_renderer(player, renderer))
        {
            fprintf(stderr, "Skipping %s picking: %s\n", marcus_renderer_name(renderer), marcus_error(player));
            continue;
        }
        CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
        CHECK(marcus_pick(player, &image, pick_x, pick_y, &picked) && picked == expected_pick);
    }
    CHECK(marcus_set_renderer(player, MARCUS_RENDERER_BRENDER));
    CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    CHECK(marcus_pick(player, &pick_image, pick_x, pick_y, &picked) && picked == expected_pick);
    CHECK(!marcus_pick(player, NULL, 0, 0, &picked) && picked == -1);
    CHECK(!marcus_pick(player, &pick_image, 0, 0, NULL));
    CHECK(marcus_seek(player, 10));
    CHECK(!marcus_pick(player, &pick_image, pick_x, pick_y, &picked) && picked == -1);
    CHECK(marcus_seek(player, 10) && marcus_object(player, 0, &later));
    CHECK(later.x != first.x || later.y != first.y || later.z != first.z || later.cel != first.cel);
    CHECK(marcus_object_bounds(player, 0, &bounds) && bounds.valid);
    CHECK(memcmp(first_bounds.corners, bounds.corners, sizeof(bounds.corners)));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(image.scene_camera.display_depth == guide_depth[0]);
    CHECK(memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_seek(player, 1));
    CHECK(marcus_object_bounds(player, 0, &bounds) && bounds.valid);
    CHECK(!memcmp(first_bounds.corners, bounds.corners, sizeof(bounds.corners)));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(!memcmp(authored.transform, image.scene_camera.transform, sizeof(authored.transform)));
    CHECK(marcus_seek(player, 16) && marcus_info(player, &info) && info.camera == 1);
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(image.scene_camera.valid &&
          (memcmp(authored.transform, image.scene_camera.transform, sizeof(authored.transform)) ||
           authored.vertical_fov != image.scene_camera.vertical_fov));
    guide_depth[1] = image.scene_camera.display_depth;
    CHECK(marcus_seek(player, 1));
    CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    orbit = image.projection;
    for (index = 0; index < (int)(sizeof(frames) / sizeof(frames[0])); ++index)
    {
        CHECK(marcus_seek(player, frames[index]));
        CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
        CHECK(!memcmp(orbit.transform, image.projection.transform, sizeof(orbit.transform)));
        CHECK(orbit.near_clip == image.projection.near_clip && orbit.far_clip == image.projection.far_clip);
        CHECK(marcus_info(player, &info));
        CHECK(image.scene_camera.display_depth == guide_depth[info.camera]);
    }
    CHECK(marcus_render(player, 480, 360, 0, 65, 5, 1.5f, &image));
    manual = image.projection;
    CHECK(memcmp(orbit.transform, manual.transform, sizeof(orbit.transform)));
    CHECK(marcus_seek(player, 10) && marcus_render(player, 480, 360, 0, 65, 5, 1.5f, &image));
    CHECK(!memcmp(manual.transform, image.projection.transform, sizeof(manual.transform)));
    CHECK(marcus_pan_orbit(player, -0.1f, 0.15f, 65, 5, 1.5f));
    CHECK(marcus_render(player, 480, 360, 0, 65, 5, 1.5f, &image));
    panned = image.projection;
    CHECK(!memcmp(manual.transform, panned.transform, 9 * sizeof(float)));
    CHECK(memcmp(manual.transform + 9, panned.transform + 9, 3 * sizeof(float)));
    CHECK(manual.near_clip == panned.near_clip && manual.far_clip == panned.far_clip);
    CHECK(marcus_seek(player, 25) && marcus_render(player, 480, 360, 0, 65, 5, 1.5f, &image));
    CHECK(!memcmp(panned.transform, image.projection.transform, sizeof(panned.transform)));
    CHECK(marcus_seek(player, 1));
    marcus_reset_framing(player);
    CHECK(!marcus_pan_orbit(player, 0.1f, 0, 25, 12, 1));
    CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    CHECK(!memcmp(orbit.transform, image.projection.transform, sizeof(orbit.transform)));
    CHECK(marcus_seek(player, 1) && marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    camera = image.camera;
    CHECK(camera.move_speed > 0);
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(fabs(image.camera.x - camera.x) < 0.001 && fabs(image.camera.z - camera.z) < 0.001);
    flight = image.projection;
    memcpy(pixels, image.rgba, bytes);
    moved = camera;
    moved.x += camera.move_speed * 0.5f;
    CHECK(marcus_render_fly(player, 480, 360, &moved, &image));
    CHECK(memcmp(pixels, image.rgba, bytes));
    moved = camera;
    moved.yaw += 20;
    CHECK(marcus_render_fly(player, 480, 360, &moved, &image));
    CHECK(memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_render_fly(player, 480, 360, &image.camera, &image));
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_seek(player, 10));
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(fabs(image.camera.x - camera.x) < 0.001 && fabs(image.camera.z - camera.z) < 0.001);
    CHECK(flight.near_clip == image.projection.near_clip && flight.far_clip == image.projection.far_clip);
    CHECK(image.scene_camera.display_depth == guide_depth[0]);
    CHECK(marcus_seek(player, 20) && marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(flight.near_clip == image.projection.near_clip && flight.far_clip == image.projection.far_clip);
    CHECK(image.scene_camera.display_depth == guide_depth[1]);
    CHECK(marcus_seek(player, 1));
    /* Both views share a frame, but moving the work camera must not alter the
     * movie camera, its pixels, or the authored camera selection. */
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    memcpy(pixels, image.rgba, bytes);
    authored = image.scene_camera;
    moved = camera;
    moved.x += camera.move_speed;
    moved.yaw += 25;
    CHECK(marcus_render_fly(player, 480, 360, &moved, &image));
    CHECK(marcus_info(player, &info) && info.frame == 1 && info.camera == 0);
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(!memcmp(authored.transform, image.scene_camera.transform, sizeof(authored.transform)));
    /* Reset inspection to the shot, preserving its pose and lens instead of
     * fitting all actor bounds. Orbit and Fly must start at that same pose. */
    CHECK(marcus_match_camera(player, 480, 360, &camera));
    CHECK(fabs(camera.x - authored.transform[9]) < .001);
    CHECK(fabs(camera.y - authored.transform[10]) < .001);
    CHECK(fabs(camera.z - authored.transform[11]) < .001);
    CHECK(marcus_render(player, 480, 360, 0, camera.yaw, camera.pitch, 1, &image));
    manual = image.projection;
    for (index = 0; index < 12; ++index)
        CHECK(fabs(manual.transform[index] - authored.transform[index]) < .02);
    CHECK(manual.far_clip == 30000);
    CHECK(fabs(manual.vertical_fov - 2 * atan(tan(authored.vertical_fov * .00872664626) *
               (authored.aspect > 4.0 / 3 ? authored.aspect / (4.0 / 3) : 1)) * 57.2957795) < .02);
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    for (index = 0; index < 12; ++index)
        CHECK(fabs(manual.transform[index] - image.projection.transform[index]) < .02);
    CHECK(fabs(manual.vertical_fov - image.projection.vertical_fov) < .001);
    hits = 0;
    for (y = 12; y < image.height; y += 24)
        for (x = 12; x < image.width; x += 24)
        {
            CHECK(marcus_pick(player, &image, x, y, &picked));
            if (picked >= 0)
            {
                CHECK(marcus_object(player, picked, &later) && later.visible);
                ++hits;
            }
        }
    CHECK(hits > 0); /* Fly uses its rendered pose rather than the movie's projection. */
    moved = camera;
    moved.x += 5;
    CHECK(marcus_render_fly(player, 480, 360, &moved, &image));
    CHECK(marcus_info(player, &info) && info.frame == 1 && info.camera == 0);
    CHECK(marcus_match_camera(player, 480, 360, &camera));
    CHECK(fabs(camera.x - authored.transform[9]) < .001);
    CHECK(!marcus_match_camera(player, 1, 1, &camera));
    CHECK(!marcus_match_camera(player, 480, 360, NULL));
    CHECK(marcus_seek(player, 20) && marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    authored = image.scene_camera;
    CHECK(marcus_match_camera(player, 480, 360, &camera));
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    for (index = 0; index < 12; ++index)
        CHECK(fabs(image.projection.transform[index] - authored.transform[index]) < .02);
    CHECK(marcus_render(player, 480, 360, 0, camera.yaw, camera.pitch, 1, &image));
    manual = image.projection;
    CHECK(marcus_pan_orbit(player, .1f, .1f, camera.yaw, camera.pitch, 1));
    CHECK(marcus_render(player, 480, 360, 0, camera.yaw, camera.pitch, 1, &image));
    CHECK(!memcmp(manual.transform, image.projection.transform, 9 * sizeof(float)));
    CHECK(memcmp(manual.transform + 9, image.projection.transform + 9, 3 * sizeof(float)));
    CHECK(marcus_seek(player, 1));
    marcus_reset_framing(player);
    moved = camera;
    moved.x = 40000;
    CHECK(!marcus_render_fly(player, 480, 360, &moved, &image) && !image.rgba);
    CHECK(!marcus_render_fly(player, 480, 360, NULL, &image) && !image.rgba);
    CHECK(!marcus_seek(player, 31) && marcus_info(player, &info) && info.frame == 1);
    CHECK(!marcus_select_scene(player, -1) && marcus_info(player, &info));
    CHECK(!marcus_select_scene(player, scene_count) && marcus_info(player, &info));
    CHECK(!marcus_open(player, "nonexistent-marcus-movie.3mm", argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.scene_count == 2 && info.frame == 1);
    other = marcus_create();
    CHECK(other && !marcus_open(other, argv[1], argv[2], argv[3]));
    marcus_destroy(other);
    other = NULL;
    for (index = 0; index < scene_count; ++index)
    {
        CHECK(marcus_select_scene(player, index));
        CHECK(marcus_info(player, &info) && info.scene == index);
        CHECK(marcus_seek(player, info.last_frame));
        CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    }
    CHECK(!marcus_render(player, 1, 1, 0, 0, 0, 1, &image) && !image.rgba);
    marcus_destroy(player);
    player = marcus_create();
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &pick_image));
    CHECK(marcus_open(player, argv[5], argv[2], argv[3]));
    CHECK(!marcus_pick(player, &pick_image, 240, 180, &picked) && picked == -1);
    CHECK(marcus_info(player, &info) && info.scene_count == 2 && info.object_count == 4);
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    input = marcus_input_create();
    CHECK(input && marcus_input_open(input, argv[6]) && marcus_input_count(input) == 2);
    CHECK(!marcus_open(player, argv[6], argv[2], argv[3]));
    CHECK(marcus_info(player, &info) && info.object_count == 4);
    CHECK(marcus_open_input(player, input, 1, argv[2], argv[3]));
    marcus_input_destroy(input);
    input = NULL;
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(marcus_info(player, &info) && info.object_count == 5);
    CHECK(marcus_open(player, argv[4], argv[2], argv[3]));
    CHECK(marcus_object(player, 4, &inactive) && !inactive.visible && !inactive.has_lifetime);
    CHECK(marcus_object_bounds(player, 4, &bounds) && !bounds.valid);
    CHECK(marcus_object(player, 1, &prop) && prop.last_frame > 10);
    CHECK(marcus_render(player, 480, 960, 1, 0, 0, 1, &image));
    CHECK(marcus_pick(player, &image, 240, 0, &picked) && picked == -1); /* Letterbox. */
    CHECK(marcus_select_scene(player, 1));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(marcus_pick(player, &image, 240, 180, &picked) && picked == -1);
    CHECK(marcus_match_camera(player, 480, 360, &camera));
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(marcus_pick(player, &image, 240, 180, &picked) && picked == -1); /* Scenery is not an actor. */
    marcus_destroy(player);
    free(pixels);
    puts("Marcus C API: scenes, actor/prop metadata, viewport picking, evaluated motion, image rewind, invalid input "
         "and renderer "
         "lifecycle passed.");
    return 0;
failed:
    marcus_input_destroy(input);
    free(pixels);
    marcus_destroy(other);
    marcus_destroy(player);
    return 1;
}
