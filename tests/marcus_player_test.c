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
    MarcusInfo info;
    MarcusScene scene;
    MarcusObject first, later, prop, inactive;
    MarcusImage image;
    MarcusCamera camera, moved;
    unsigned char *pixels;
    size_t bytes;
    int index, scene_count;
    if (argc != 5)
        return 2;
    player = marcus_create();
    other = NULL;
    pixels = NULL;
    CHECK(player);
    CHECK(!marcus_info(player, &info));
    CHECK(!marcus_seek(player, 1));
    CHECK(!marcus_scene(player, -1, &scene));
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]));
    CHECK(marcus_info(player, &info));
    scene_count = info.scene_count;
    CHECK(info.scene_count == 2 && info.object_count == 4 && info.first_frame == 1 && info.last_frame == 30);
    CHECK(marcus_scene(player, 0, &scene) && scene.first_frame == 1 && scene.last_frame == 30);
    CHECK(marcus_object(player, 0, &first));
    CHECK(!strcmp(first.name, "Animated actor") && first.visible && !first.prop && first.has_lifetime);
    CHECK(marcus_object(player, 1, &prop) && prop.prop);
    CHECK(!marcus_object(player, -1, &later));
    CHECK(!marcus_object(player, 4, &later));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(image.width == 480 && image.height == 360 && image.pitch == 480 * 4 && image.rgba);
    bytes = (size_t)image.pitch * image.height;
    pixels = (unsigned char *)malloc(bytes);
    CHECK(pixels);
    memcpy(pixels, image.rgba, bytes);
    CHECK(marcus_seek(player, 10) && marcus_object(player, 0, &later));
    CHECK(later.x != first.x || later.y != first.y || later.z != first.z || later.cel != first.cel);
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_seek(player, 1));
    CHECK(marcus_render(player, 480, 360, 1, 0, 0, 1, &image));
    CHECK(!memcmp(pixels, image.rgba, bytes));
    CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &image));
    camera = image.camera;
    CHECK(camera.move_speed > 0);
    CHECK(marcus_render_fly(player, 480, 360, &camera, &image));
    CHECK(fabs(image.camera.x - camera.x) < 0.001 && fabs(image.camera.z - camera.z) < 0.001);
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
    CHECK(marcus_seek(player, 1));
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
    CHECK(marcus_open(player, argv[4], argv[2], argv[3]));
    CHECK(marcus_object(player, 4, &inactive) && !inactive.visible && !inactive.has_lifetime);
    CHECK(marcus_object(player, 1, &prop) && prop.last_frame > 10);
    marcus_destroy(player);
    free(pixels);
    puts("Marcus C API: scenes, actor/prop metadata, evaluated motion, image rewind, invalid input and renderer "
         "lifecycle passed.");
    return 0;
failed:
    free(pixels);
    marcus_destroy(other);
    marcus_destroy(player);
    return 1;
}
