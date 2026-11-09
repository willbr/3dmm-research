#include "player.h"
#include <stdio.h>
#include <stdlib.h>

static void json_string(const char *text)
{
    const unsigned char *cursor;
    putchar('"');
    for (cursor = (const unsigned char *)text; *cursor; ++cursor)
    {
        if (*cursor == '"' || *cursor == '\\')
            printf("\\%c", *cursor);
        else if (*cursor < 32)
            printf("\\u%04x", (unsigned int)*cursor);
        else
            putchar(*cursor);
    }
    putchar('"');
}

static void failure(MarcusPlayer *player, const char *phase, int scene, long frame)
{
    printf("MARCUS_BATCH {\"event\":\"error\",\"phase\":\"%s\",\"scene\":%d,\"raw_frame\":%ld,\"message\":", phase,
           scene, frame);
    json_string(marcus_error(player));
    puts("}");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    MarcusPlayer *player;
    MarcusScene scene;
    MarcusInfo info;
    MarcusImage image;
    int index, count, errors, opened, complete;
    long frame;
    double rendered, expected;
    if (argc != 4)
    {
        fputs("Usage: marcus-playback MOVIE RESOURCES CONTENT_DIRECTORY\n", stderr);
        return 2;
    }
    player = marcus_create();
    if (!player)
        return 2;
    puts("MARCUS_BATCH {\"event\":\"start\"}");
    fflush(stdout);
    errors = count = 0;
    rendered = expected = 0;
    opened = marcus_open(player, argv[1], argv[2], argv[3]);
    if (!opened)
    {
        failure(player, "open", 0, 0);
        ++errors;
    }
    /* Scene metadata can survive a failed initial scene selection. Continue
     * through the remaining scenes instead of losing the rest of the movie. */
    for (index = 0; marcus_scene(player, index, &scene); ++index)
    {
        ++count;
        expected += (double)scene.last_frame - scene.first_frame + 1;
        printf("MARCUS_BATCH {\"event\":\"scene\",\"scene\":%d,\"first_frame\":%ld,\"last_frame\":%ld}\n", index + 1,
               scene.first_frame, scene.last_frame);
        fflush(stdout);
        if (!marcus_select_scene(player, index))
        {
            failure(player, "select", index + 1, scene.first_frame);
            ++errors;
            continue;
        }
        complete = 1;
        for (frame = scene.first_frame;; ++frame)
        {
            /* Journal before evaluation, so a crash or hang has an exact
             * scene and frame checkpoint. Do not pace at movie speed. */
            printf("MARCUS_BATCH {\"event\":\"frame\",\"scene\":%d,\"raw_frame\":%ld,\"frame\":%.0f}\n", index + 1,
                   frame, (double)frame - scene.first_frame + 1);
            fflush(stdout);
            if (!marcus_seek(player, frame) || !marcus_info(player, &info))
            {
                failure(player, "seek", index + 1, frame);
                ++errors;
                complete = 0;
                break;
            }
            if (!marcus_render(player, 480, 360, 1, 0, 0, 1, &image))
            {
                failure(player, "render", index + 1, frame);
                ++errors;
                complete = 0;
                break;
            }
            ++rendered;
            if (frame == scene.last_frame)
                break;
        }
        printf("MARCUS_BATCH {\"event\":\"scene_done\",\"scene\":%d,\"complete\":%s}\n", index + 1,
               complete ? "true" : "false");
        fflush(stdout);
    }
    printf("MARCUS_BATCH {\"event\":\"done\",\"scenes\":%d,\"frames_rendered\":%.0f,\"frames_expected\":%.0f,"
           "\"errors\":%d}\n",
           count, rendered, expected, errors);
    marcus_destroy(player);
    return errors || !count ? 1 : 0;
}
