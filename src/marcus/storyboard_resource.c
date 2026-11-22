#include "storyboard_resource.h"
#include "scene_renderer.h"
#include "mm/background_resource.h"
#include "mm/collections.h"
#include "mm/depth_bitmap.h"
#include "mm/masked_bitmap.h"
#include <SDL3/SDL_stdinc.h>
#include <stdlib.h>
#include <string.h>

enum
{
    BLACK_BACKGROUND = 11,
    PALETTE_BASE = 10, PALETTE_COUNT = 236, COLOUR_BYTES = 4, ARRAY_HEADER = 12,
    SOUND_BYTES = 28,
    PAINT_BITMAP = 0x4d424d50, DEPTH_BITMAP = 0x5a424d50
};

unsigned char marcus_board_paint_index(const MarcusBoardInk *ink, int x, int y)
{
    static const int pattern[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return pattern[(y & 3) * 4 + (x & 3)] < ink->amount ? ink->second : ink->first;
}

MarcusBoardInk marcus_board_match_colour(const unsigned palette[256], unsigned rgb)
{
    const unsigned end = PALETTE_BASE + PALETTE_COUNT;
    MarcusBoardInk result = {0};
    double best = 1e20;
    unsigned a, b;
    for (a = PALETTE_BASE; a < end; ++a)
    {
        unsigned distance = 0;
        int shift;
        for (shift = 0; shift <= 16; shift += 8)
        {
            int difference = (int)((palette[a] >> shift) & 255) - (int)((rgb >> shift) & 255);
            distance += difference * difference;
        }
        if (distance < best)
        {
            best = distance;
            result.first = result.second = (unsigned char)a;
            result.rgb = palette[a];
        }
    }
    /* Penalize contrast to favour mixtures of nearby palette colours. */
    for (a = PALETTE_BASE; a < end && best > 0; ++a)
        for (b = a + 1; b < end; ++b)
        {
            int difference[3], length = 0, projection = 0, c, amount;
            double score;
            unsigned mixed_rgb = 0;
            for (c = 0; c < 3; ++c)
            {
                int shift = c * 8, first = (int)((palette[a] >> shift) & 255);
                int target = (int)((rgb >> shift) & 255) - first;
                difference[c] = (int)((palette[b] >> shift) & 255) - first;
                length += difference[c] * difference[c];
                projection += target * difference[c];
            }
            if (projection <= 0 || projection >= length)
                continue;
            amount = (int)SDL_lround((double)projection * 16 / length);
            if (amount <= 0 || amount >= 16)
                continue;
            score = .01 * length * amount * (16 - amount) / 256.;
            for (c = 0; c < 3; ++c)
            {
                int shift = c * 8, first = (int)((palette[a] >> shift) & 255);
                int mixed = (first * 16 + difference[c] * amount + 8) / 16;
                int error = mixed - (int)((rgb >> shift) & 255);
                score += error * error;
                mixed_rgb |= (unsigned)mixed << shift;
            }
            if (score < best)
            {
                best = score;
                result.first = (unsigned char)a;
                result.second = (unsigned char)b;
                result.amount = (unsigned char)amount;
                result.rgb = mixed_rgb;
            }
        }
    return result;
}

static void put16(unsigned char *p, unsigned value, int big)
{
    p[big ? 1 : 0] = (unsigned char)value;
    p[big ? 0 : 1] = (unsigned char)(value >> 8);
}

static void put32(unsigned char *p, MMI32 value, int big)
{
    MMU32 word = (MMU32)value;
    put16(p, (unsigned)(big ? word >> 16 : word), big);
    put16(p + 2, (unsigned)(big ? word : word >> 16), big);
}

static void resource_header(unsigned char *bytes, const MMMovieArchiveEditor *editor)
{
    put16(bytes, 1, editor->big_endian);
    put16(bytes + 2, editor->system, editor->big_endian);
}

static void put_matrix(unsigned char *bytes, const MMActorMatrix *matrix, int big)
{
    int i;
    for (i = 0; i < 12; ++i)
        put32(bytes + 4 * i, matrix->m[i / 3][i % 3], big);
}

static unsigned short angle(float degrees)
{
    return (unsigned short)(int)(degrees * (65536.f / 360));
}

static MMI32 fixed(float value)
{
    return (MMI32)(value * 65536.f);
}

static int add(const MMMovieArchiveEditor *editor, MMU32 type, const void *data, MMU32 size, MMU32 *number)
{
    MMStorageWriter writer;
    return editor->add(editor->context, type, size, number, &writer) &&
           writer.write_at(writer.context, 0, data, size);
}

static int add_child(const MMMovieArchiveEditor *editor, MMU32 parent_type, MMU32 parent,
                     MMU32 type, MMU32 slot, const void *data, MMU32 size, MMU32 *number)
{
    MMChunkChild child;
    if (!add(editor, type, data, size, number))
        return 0;
    child.type = type;
    child.number = *number;
    child.slot = slot;
    return mm_chunk_adopt(&editor->edit, parent_type, parent, &child, 1) == MM_CHUNK_OK;
}

static int read_reference(const MMMovieArchiveEditor *retail, MMU32 type, MMReadStream *stream)
{
    MMChunkAccess chunks = mm_chunk_edit_access(&retail->edit);
    MMU32 number;
    return mm_chunk_link_number(&chunks, MM_BACKGROUND, BLACK_BACKGROUND, 0, type, &number) == MM_CHUNK_OK &&
           retail->read(retail->context, type, number, stream);
}

static int make_palette(const MMMovieArchiveEditor *resources, const MMMovieArchiveEditor *retail,
                        const MMMovieArchiveEditor *target, unsigned char *bytes, unsigned palette[256])
{
    MMReadStream stream;
    MMCollectionHeader table;
    MMBackgroundHeader black;
    unsigned i;
    if (!resources->read(resources->context, MM_BACKGROUND_COLORS, 0, &stream) ||
        mm_collection_header_read(&stream, MM_COLLECTION_ARRAY, 0, &table) != MM_STORAGE_OK ||
        table.width != COLOUR_BYTES || table.count < 256)
        return 0;
    /* The UI palette is required by the app. The authored range comes from the
     * black scene's shared actor/background palette. */
    if (!retail->read(retail->context, MM_BACKGROUND, BLACK_BACKGROUND, &stream) ||
        !mm_background_header_read(&stream, &black) || black.palette_base != PALETTE_BASE ||
        !read_reference(retail, MM_BACKGROUND_COLORS, &stream) ||
        mm_collection_header_read(&stream, MM_COLLECTION_ARRAY, 0, &table) != MM_STORAGE_OK ||
        table.width != COLOUR_BYTES || table.count != PALETTE_COUNT ||
        !mm_read_exact(&stream, ARRAY_HEADER, bytes + ARRAY_HEADER, table.first_bytes))
        return 0;
    for (i = 0; i < PALETTE_COUNT; ++i)
    {
        const unsigned char *colour = bytes + ARRAY_HEADER + COLOUR_BYTES * i;
        palette[PALETTE_BASE + i] = ((unsigned)colour[2] << 16) | ((unsigned)colour[1] << 8) | colour[0];
    }
    table.system = target->system;
    table.big_endian = target->big_endian;
    return mm_collection_header_write(&table, bytes, ARRAY_HEADER) == MM_STORAGE_OK;
}

static int write_lights(const MMMovieArchiveEditor *target, MMU32 background,
                        const MarcusBoardEditValues *values)
{
    unsigned char bytes[MM_BACKGROUND_LIGHT_TABLE_BYTES];
    MMBackgroundLightRig rig;
    MMU32 number, size;
    if (!mm_background_light_rig_prepare(values->lights, (size_t)values->light_count,
                                         mm_brender_background_light_rotation, &rig))
        return 0;
    size = mm_background_light_rig_encode(&rig, target->system, target->big_endian, bytes, sizeof(bytes));
    return size && add_child(target, MM_BACKGROUND, background, MM_BACKGROUND_LIGHTS, 0, bytes, size, &number);
}

static int allocate(void *context, MMI32 bytes, void **memory)
{
    (void)context;
    *memory = malloc((size_t)bytes);
    return *memory != NULL;
}

static void free_memory(void *context, void **memory)
{
    (void)context;
    free(*memory);
    *memory = NULL;
}

static int add_depth(void *context, void *file, MMU32 type, MMU32 bytes, MMU32 *number, MMStorageWriter *writer)
{
    const MMMovieArchiveEditor *target = (const MMMovieArchiveEditor *)file;
    (void)context;
    return target->add(target->context, type, bytes, number, writer);
}

static int write_depth(const MMMovieArchiveEditor *target, MMU32 *number)
{
    MMDepthBitmap depth = {{0, 0, MARCUS_BOARD_WIDTH, MARCUS_BOARD_HEIGHT},
                           MARCUS_BOARD_WIDTH * 2, MARCUS_BOARD_PIXELS * 2, NULL};
    MMDepthWriteServices services = {0};
    int ok;
    depth.pixels = malloc((size_t)depth.bytes);
    if (!depth.pixels)
        return 0;
    memset(depth.pixels, 255, (size_t)depth.bytes);
    services.system = target->system;
    services.host_big_endian = target->big_endian;
    services.add = add_depth;
    ok = mm_depth_write(&depth, (void *)target, DEPTH_BITMAP, number, &services);
    free(depth.pixels);
    return ok;
}

static int write_paint(const MMMovieArchiveEditor *target, MMU32 camera, MMU32 depth,
                       const unsigned char *pixels, const MarcusBoardInk *inks, unsigned char *paint)
{
    const MMMaskedRect bounds = {0, 0, MARCUS_BOARD_WIDTH, MARCUS_BOARD_HEIGHT};
    MMMaskedBitmap bitmap = {0};
    MMMaskedServices services = {0};
    MMChunkChild depth_link = {DEPTH_BITMAP, 0, 0};
    MMU32 image;
    int i, ok;
    depth_link.number = depth;
    for (i = 0; i < MARCUS_BOARD_PIXELS; ++i)
        paint[i] = marcus_board_paint_index(&inks[pixels[i]], i % MARCUS_BOARD_WIDTH, i / MARCUS_BOARD_WIDTH);
    services.system = target->system;
    services.host_big_endian = target->big_endian;
    services.allocate = allocate;
    services.free_memory = free_memory;
    ok = mm_masked_init(&bitmap, paint, MARCUS_BOARD_WIDTH, MARCUS_BOARD_HEIGHT, &bounds, 0, 0, 255, 0, 0, &services) &&
         add_child(target, MM_BACKGROUND_CAMERA, camera, PAINT_BITMAP, 0, bitmap.data, (MMU32)bitmap.bytes, &image) &&
         mm_chunk_adopt(&target->edit, MM_BACKGROUND_CAMERA, camera, &depth_link, 1) == MM_CHUNK_OK;
    mm_masked_dispose(&bitmap, &services);
    return ok;
}

static void make_camera(const MMMovieArchiveEditor *target, const MarcusBoardPose *pose, unsigned short ref_fov,
                        const MMActorPoint *points, MMI32 count, unsigned char *bytes)
{
    const int big = target->big_endian;
    const float margin = (pose->far_clip - pose->near_clip) * .001f;
    const float lens = SDL_tanf(pose->fov * .00872664626f) / SDL_tanf(ref_fov * (3.14159265358979323846f / 65536));
    MMActorMatrix matrix;
    MMI32 i;
    mm_brender_camera_matrix(&matrix, pose->camera.x, pose->camera.y, pose->camera.z,
                            angle(pose->camera.yaw), angle(-pose->camera.pitch), angle(pose->roll));
    resource_header(bytes, target);
    put32(bytes + 4, fixed(pose->near_clip), big);
    put32(bytes + 8, fixed(pose->far_clip), big);
    put16(bytes + 12, angle(pose->fov), big);
    put16(bytes + 14, 0, big);
    put_matrix(bytes + 28, &matrix, big);
    /* Adapt the retail insertion anchors to the new lens in camera space. */
    for (i = 0; i < count; ++i)
    {
        const float original = -(points[i].dzr / 65536.f);
        float distance = original, scale;
        MMActorPoint local, world;
        unsigned char *point = bytes + (i ? MM_BACKGROUND_CAMERA_BYTES + (i - 1) * MM_BACKGROUND_POINT_BYTES : 16);
        if (distance < pose->near_clip + margin)
            distance = pose->near_clip + margin;
        if (distance > pose->far_clip - margin)
            distance = pose->far_clip - margin;
        scale = lens * distance / original;
        local.dxr = fixed((points[i].dxr / 65536.f) * scale);
        local.dyr = fixed((points[i].dyr / 65536.f) * scale);
        local.dzr = fixed(-distance);
        mm_brender_matrix_apply_point(&world, &local, &matrix);
        put32(point, world.dxr, big);
        put32(point + 4, world.dyr, big);
        put32(point + 8, world.dzr, big);
    }
}

static const char *write_cameras(const MarcusBoardWire *board, const MMMovieArchiveEditor *retail,
                                 const MMMovieArchiveEditor *target, const MarcusBoardInk *inks,
                                 MMU32 background, MMU32 depth)
{
    MMReadStream stream;
    MMBackgroundCameraData camera;
    MMActorMatrix inverse;
    MMActorPoint *points = NULL;
    unsigned char *bytes = NULL, *paint = NULL;
    const char *error;
    MMI32 i;
    if (!read_reference(retail, MM_BACKGROUND_CAMERA, &stream) || !mm_background_camera_decode(&stream, &camera))
        return "Cannot read the black scene's insertion camera.";
    if (!mm_brender_matrix_inverse(&inverse, &camera.matrix))
        return "Invalid black scene camera transform.";
    error = "Cannot allocate storyboard camera and painting.";
    points = (MMActorPoint *)malloc((size_t)(camera.point_count + 1) * sizeof(*points));
    bytes = (unsigned char *)malloc(stream.length);
    paint = (unsigned char *)malloc(MARCUS_BOARD_PIXELS);
    if (!points || !bytes || !paint)
        goto done;
    for (i = 0; i <= camera.point_count; ++i)
    {
        MMActorPoint place = camera.place;
        error = "Cannot read an extra black scene insertion point.";
        if (i && !mm_background_point_decode(&stream, &camera, i - 1, &place))
            goto done;
        error = "Invalid black scene insertion depth.";
        mm_brender_matrix_apply_point(&points[i], &place, &inverse);
        if (points[i].dzr >= 0)
            goto done;
    }
    for (i = 0; i < board->values.angle_count; ++i)
    {
        MMU32 number;
        make_camera(target, &board->values.angles[i], camera.fov, points, camera.point_count + 1, bytes);
        error = "Cannot write storyboard camera.";
        if (!add_child(target, MM_BACKGROUND, background, MM_BACKGROUND_CAMERA, (MMU32)i, bytes, stream.length, &number))
            goto done;
        error = "Cannot write storyboard painting.";
        if (!write_paint(target, number, depth, board->pixels[i], inks, paint))
            goto done;
    }
    error = NULL;
done:
    free(points);
    free(bytes);
    free(paint);
    return error;
}

const char *marcus_board_write_resources(const MarcusBoardWire *board, const MMMovieArchiveEditor *resources,
                                         const MMMovieArchiveEditor *retail, const MMMovieArchiveEditor *target,
                                         MMU32 *background)
{
    static const unsigned short wide_name[] = {'S', 't', 'o', 'r', 'y', 'b', 'o', 'a', 'r', 'd'};
    MMReadStream stream;
    MMMovieTitle title;
    unsigned char header[8] = {0}, sound[SOUND_BYTES], colours[ARRAY_HEADER + COLOUR_BYTES * PALETTE_COUNT];
    unsigned palette[256] = {0};
    MarcusBoardInk inks[MARCUS_BOARD_COLOUR_LIMIT];
    MMU32 number, child, depth;
    const char *error;
    int i;
    if (!marcus_board_wire_size(board))
        return "Invalid storyboard.";
    if (!make_palette(resources, retail, target, colours, palette))
        return "Cannot read the movie and retail black scene palettes.";
    for (i = 0; i < board->values.colour_count; ++i)
        inks[i] = marcus_board_match_colour(palette, board->values.colours[i]);
    resource_header(header, target);
    header[4] = PALETTE_BASE;
    if (!add(target, MM_BACKGROUND, header, sizeof(header), &number))
        return "Cannot write storyboard background.";
    if (!mm_movie_title_init(&title, target->text_unit) ||
        !mm_movie_title_set(&title, target->text_unit == 1 ? (const void *)"Storyboard" : (const void *)wide_name, 10) ||
        !target->set_title(target->context, MM_BACKGROUND, number, &title))
        return "Cannot name storyboard background.";
    if (!add_child(target, MM_BACKGROUND, number, MM_BACKGROUND_COLORS, 0, colours, sizeof(colours), &child))
        return "Cannot write storyboard colours.";
    if (!write_lights(target, number, &board->values))
        return "Cannot write storyboard lights.";
    if (!write_depth(target, &depth))
        return "Cannot write empty storyboard depth.";
    error = write_cameras(board, retail, target, inks, number, depth);
    if (error)
        return error;
    /* Retail and v3dmm collect this child as a tag. Keep the base-content sound
     * reference muted and outside the timeline. */
    if (!read_reference(retail, MM_BACKGROUND_SOUND, &stream) || !mm_read_exact(&stream, 0, sound, sizeof(sound)))
        return "Cannot read the black scene's default sound.";
    memset(sound + 4, 0, 8); /* Volume and loop flag. */
    if (!add_child(target, MM_BACKGROUND, number, MM_BACKGROUND_SOUND, 0, sound, sizeof(sound), &child))
        return "Cannot write storyboard default sound.";
    *background = number;
    return NULL;
}
