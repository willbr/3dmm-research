#include "movie_session_internal.h"
#include "mm/action_definition.h"
#include "mm/font_resource.h"
#include "mm/string_wire.h"
#include "mm/template_runtime.h"
#include "mm/text_generation.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

static MMI32 signed_short(const unsigned char *bytes, int big)
{
    MMU32 value = big ? (MMU32)bytes[0] * 256 + bytes[1] : (MMU32)bytes[1] * 256 + bytes[0];
    return value < 32768 ? (MMI32)value : (MMI32)value - 65536L;
}

static int reserve_parts(MMMovieEvaluatedBody *body, MMU32 count)
{
    if ((size_t)count != count || (count && (size_t)-1 / (size_t)count < sizeof(*body->parts)))
        return MM_MOVIE_MEMORY;
    body->parts = count ? (MMMovieBodyPart *)calloc((size_t)count, sizeof(*body->parts)) : NULL;
    if (count && !body->parts)
        return MM_MOVIE_MEMORY;
    body->part_count = count;
    return MM_MOVIE_OK;
}

void mm_movie_body_dispose(MMMovieEvaluatedBody *body)
{
    if (body)
    {
        free(body->parts);
        free(body);
    }
}

static int body_child(const MMChunkAccess *access, MMU32 parent, MMU32 slot, MMU32 type, MMChunkEntry *entry)
{
    MMU32 position;
    int status = mm_session_child(access, parent, slot, type, entry, &position);
    return status == MM_CHUNK_OK ? MM_MOVIE_OK
         : status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
}

static int array(const MMMovieBodyInput *in, const MMChunkAccess *access, MMU32 parent, MMU32 type,
                 MMU32 width, MMMoviePayload *data, MMCollectionHeader *head)
{
    MMChunkEntry entry;
    int status = body_child(access, parent, 0, type, &entry);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_session_payload(in->archive, &entry, 0, data);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_collection_header_read(data->stream, MM_COLLECTION_ARRAY, 0, head);
    if (status == MM_STORAGE_OK && head->width == width)
        return MM_MOVIE_OK;
    free(data->decoded);
    data->decoded = NULL;
    return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_BAD_FILE;
}

int mm_session_body_shape(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body, const MMActorHeader *actor)
{
    const MMChunkAccess *access = &in->archive->access;
    MMMoviePayload parents, sets;
    MMCollectionHeader parent_head, set_head;
    unsigned char parent[2], set[2];
    MMMovieBodyPart *part;
    MMU32 i;
    int status;
    parents.decoded = sets.decoded = NULL;
    status = array(in, access, in->template_position, MM_GLPI, 2, &parents, &parent_head);
    if (status == MM_MOVIE_OK)
        status = array(in, access, in->template_position, MM_GLBS, 2, &sets, &set_head);
    if (status != MM_MOVIE_OK)
        goto done;
    if (parent_head.count != set_head.count)
    {
        status = MM_MOVIE_BAD_FILE;
        goto done;
    }
    status = reserve_parts(body, parent_head.count);
    if (status != MM_MOVIE_OK)
        goto done;
    body->model_owner.source = actor->source_id;
    body->model_owner.type = actor->template_type;
    body->model_owner.number = actor->template_number;
    for (i = 0; i < parent_head.count; ++i)
    {
        part = &body->parts[i];
        if (!mm_read_exact(parents.stream, parent_head.header_bytes + i * 2, parent, sizeof(parent)) ||
            !mm_read_exact(sets.stream, set_head.header_bytes + i * 2, set, sizeof(set)))
        {
            status = MM_MOVIE_IO;
            goto done;
        }
        part->parent = signed_short(parent, parent_head.big_endian);
        part->set = signed_short(set, set_head.big_endian);
        if (part->set < 0)
        {
            status = MM_MOVIE_BAD_FILE;
            goto done;
        }
    }
done:
    free(parents.decoded);
    free(sets.decoded);
    return status;
}

typedef struct MovieBodyCel
{
    const MMMovieBodyInput *input;
    MMMovieEvaluatedBody *body;
    MMChunkAccess access;
    MMMoviePayload matrices;
    MMCollectionHeader matrix_head;
    const unsigned char *parts;
    int status;
} MovieBodyCel;

static void movie_cel_part(void *context, MMU32 ordinal, MMI32 *model, MMI32 *matrix)
{
    const MovieBodyCel *cel = (const MovieBodyCel *)context;
    const unsigned char *row = cel->parts + ordinal * 4;
    int big = cel->input->cels->header.big_endian;
    *model = signed_short(row, big);
    *matrix = signed_short(row + 2, big);
}

static int movie_cel_prepare(void *context, MMU32 ordinal, MMI32 model, MMI32 matrix, int replace_model)
{
    MovieBodyCel *cel = (MovieBodyCel *)context;
    MMMovieBodyPart *part = &cel->body->parts[ordinal];
    const MMCollectionHeader *head = &cel->matrix_head;
    MMChunkEntry entry;
    unsigned char bytes[48];
    MMU32 j;
    if (model < MM_TEMPLATE_KEEP_MODEL || matrix < 0 || (MMU32)matrix >= head->count)
    {
        cel->status = MM_MOVIE_BAD_FILE;
        return 0;
    }
    if (replace_model)
    {
        cel->status = body_child(&cel->access, cel->input->template_position, (MMU32)model, MM_TEMPLATE_BMDL, &entry);
        if (cel->status != MM_MOVIE_OK)
            return 0;
        part->model.source = cel->body->model_owner.source;
        part->model.type = entry.type;
        part->model.number = entry.number;
    }
    if (!mm_read_exact(cel->matrices.stream, head->header_bytes + (MMU32)matrix * sizeof(bytes), bytes, sizeof(bytes)))
    {
        cel->status = MM_MOVIE_IO;
        return 0;
    }
    for (j = 0; j < 12; ++j)
        part->matrix.m[j / 3][j % 3] = mm_signed_word(mm_session_word(bytes + j * 4, head->big_endian));
    return 1;
}

static void movie_cel_apply(void *context, MMU32 ordinal, int replace_model, MMI32 matrix)
{
    MovieBodyCel *cel = (MovieBodyCel *)context;
    MMMovieBodyPart *part = &cel->body->parts[ordinal];
    (void)matrix;
    if (replace_model)
        part->active_model = part->model;
    else
        memset(&part->model, 0, sizeof(part->model));
}

int mm_session_body_cel(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body)
{
    MovieBodyCel cel;
    MMTemplateCelServices bindings;
    const unsigned char *row;
    MMI32 variable;
    row = mm_session_group_row(in->cels, (MMU32)in->cel, &variable);
    if (!row || variable < 0 || body->part_count > (MMU32)variable / 4)
        return MM_MOVIE_BAD_FILE;
    cel.input = in;
    cel.body = body;
    cel.access = in->archive->access;
    cel.parts = row + in->cels->header.width;
    cel.status = array(in, &cel.access, in->action_position, MM_ACTION_GLXF, 48, &cel.matrices, &cel.matrix_head);
    if (cel.status != MM_MOVIE_OK)
        return cel.status;
    bindings.context = &cel;
    bindings.part = movie_cel_part;
    bindings.prepare_part = movie_cel_prepare;
    bindings.begin_apply = NULL;
    bindings.apply_part = movie_cel_apply;
    mm_template_bind_cel(body->part_count, &bindings);
    free(cel.matrices.decoded);
    return cel.status;
}

typedef struct TextBody
{
    MMMovieEvaluatedBody *body;
    unsigned char string[MM_STRING_MAX + 2];
    MMFontResource metrics;
    MMI32 shape, cel;
    int status;
} TextBody;

static MMI32 legacy_ansi(void *context, const void *bytes, MMU32 count, int encoding, void *out, MMU32 capacity)
{
    (void)context;
    return (MMI32)mm_text_ansi(NULL, bytes, (size_t)count, encoding, (char *)out, (size_t)capacity);
}

static int text_name(const MMMovieBodyInput *in, TextBody *text)
{
    const unsigned short one = 1;
    MMMoviePayload name;
    MMStringWire wire;
    int status = mm_session_name(in->archive, in->template_position, &name);
    if (status != MM_MOVIE_OK)
        return status;
    wire.buffer = text->string;
    wire.unit = 1;
    wire.current_encoding = MM_TEXT_WINDOWS;
    wire.native_big_endian = *(const unsigned char *)&one == 0;
    wire.swapped_unicode_encoding = wire.native_big_endian ? MM_TEXT_UTF16_LE : MM_TEXT_UTF16_BE;
    wire.convert = legacy_ansi;
    wire.context = NULL;
    status = mm_string_wire_read(&wire, name.stream, 0, NULL) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
    free(name.decoded);
    return status;
}

static int font_unpacked(void *p, void *block) { (void)p; (void)block; return 1; }
static MMI32 font_size(void *p, void *block) { (void)p; return (MMI32)((const MMReadStream *)block)->length; }
static int font_read(void *p, void *block, void *out, MMI32 count, MMI32 offset)
{
    int ok = mm_read_exact((const MMReadStream *)block, (MMU32)offset, out, (size_t)count);
    if (!ok)
        ((TextBody *)p)->status = MM_MOVIE_IO;
    return ok;
}
static int font_allocate(void *p, MMI32 bytes, int clear, void **out)
{
    (void)clear;
    *out = bytes ? malloc((size_t)bytes) : NULL;
    if (bytes && !*out)
        ((TextBody *)p)->status = MM_MOVIE_MEMORY;
    return !bytes || *out != NULL;
}
static void font_free(void *p, void **data) { (void)p; free(*data); *data = NULL; }
static void font_invalid(void *p) { ((TextBody *)p)->status = MM_MOVIE_BAD_FILE; }

static MMI32 text_count(void *p, void *object) { (void)p; return ((TextBody *)object)->string[0]; }
static MMI32 text_character(void *p, void *object, MMI32 i)
{ (void)p; return ((TextBody *)object)->string[i + 1]; }
static MMI32 text_shape(void *p, void *object) { (void)p; return ((TextBody *)object)->shape; }
static void *text_font(void *p, void *object) { (void)p; return &((TextBody *)object)->metrics; }
static MMI32 text_width(void *p, void *font, MMI32 i) { (void)p; return mm_font_width((MMFontResource *)font, i); }
static MMI32 text_height(void *p, void *font, MMI32 i) { (void)p; return mm_font_height((MMFontResource *)font, i); }
static MMI32 text_maximum(void *p, void *font) { (void)p; return ((MMFontResource *)font)->maximum_height; }
static void *text_list(void *p, MMI32 entry, MMI32 capacity) { (void)entry; (void)capacity; return p; }
static void text_list_count(void *p, void *list, MMI32 count) { (void)p; (void)list; (void)count; }
static void text_list_put(void *p, void *list, MMI32 i, const void *value)
{
    TextBody *text = (TextBody *)p;
    MMI32 count = text->string[0];
    (void)list;
    if (i / count == text->cel)
        memcpy(&text->body->parts[i % count].matrix, value, sizeof(MMActorMatrix));
}
static void text_release(void *p, void *value) { (void)p; (void)value; }

static int text_parts(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body)
{
    const unsigned short one = 1;
    const MMChunkAccess *access = &in->archive->access;
    MMMovieArchive font_archive;
    MMMoviePayload payload;
    MMChunkEntry entry;
    MMMovieAsset font;
    MMFontResourceServices font_services;
    MMTextGenerationServices generation;
    TextBody text;
    unsigned char bytes[24];
    MMU32 position, i;
    int status, big;
    memset(&text, 0, sizeof(text));
    text.body = body;
    text.cel = in->cel;
    status = text_name(in, &text);
    if (status != MM_MOVIE_OK)
        return status;
    status = body_child(access, in->template_position, 0, MM_TDT, &entry);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_session_payload(in->archive, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_read_exact(payload.stream, 0, bytes, sizeof(bytes));
    free(payload.decoded);
    if (!status)
        return MM_MOVIE_BAD_FILE;
    big = bytes[0] == 0 && bytes[1] == 1;
    if (!big && (bytes[0] != 1 || bytes[1] != 0))
        return MM_MOVIE_BAD_FILE;
    text.shape = mm_signed_word(mm_session_word(bytes + 4, big));
    if (text.shape < 0 || text.shape >= MM_TEXT_SHAPE_COUNT)
        return MM_MOVIE_BAD_FILE;
    font.source = mm_signed_word(mm_session_word(bytes + 8, big));
    font.type = mm_session_word(bytes + 16, big);
    font.number = mm_session_word(bytes + 20, big);
    if (font.source < 0 || font.type != MM_FONT_TYPE)
        return MM_MOVIE_ASSET_NOT_FOUND;
    status = mm_session_archive(in->movie, in->source, &font, &font_archive);
    if (status != MM_MOVIE_OK)
        return status;
    access = &font_archive.access;
    if (mm_chunk_find(access, font.type, font.number, &position) != MM_CHUNK_OK ||
        mm_chunk_at(access, position, &entry) != MM_CHUNK_OK)
    {
        status = MM_MOVIE_ASSET_NOT_FOUND;
        goto done;
    }
    status = mm_session_payload(&font_archive, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        goto done;
    memset(&font_services, 0, sizeof(font_services));
    font_services.context = &text;
    font_services.host_big_endian = *(const unsigned char *)&one == 0;
    font_services.unpack = font_unpacked;
    font_services.block_size = font_size;
    font_services.read = font_read;
    font_services.allocate = font_allocate;
    font_services.free_memory = font_free;
    font_services.bad_font = font_invalid;
    font_services.bad_order = font_invalid;
    status = mm_font_initialize(&text.metrics, (void *)payload.stream, &font_services);
    free(payload.decoded);
    if (!status || text.status != MM_MOVIE_OK)
    {
        status = text.status != MM_MOVIE_OK ? text.status : MM_MOVIE_BAD_FILE;
        goto done;
    }
    status = reserve_parts(body, text.string[0]);
    if (status != MM_MOVIE_OK)
        goto done;
    body->model_owner = font;
    for (i = 0; i < body->part_count; ++i)
    {
        if (text.string[i + 1] >= text.metrics.count)
        {
            status = MM_MOVIE_BAD_FILE;
            goto done;
        }
        status = body_child(access, position, text.string[i + 1], MM_TEMPLATE_BMDL, &entry);
        if (status != MM_MOVIE_OK)
            goto done;
        body->parts[i].parent = -1;
        body->parts[i].model.source = font.source;
        body->parts[i].model.type = entry.type;
        body->parts[i].model.number = entry.number;
        body->parts[i].active_model = body->parts[i].model;
    }
    memset(&generation, 0, sizeof(generation));
    generation.context = &text;
    generation.length = text_count;
    generation.character = text_character;
    generation.shape = text_shape;
    generation.list_new = text_list;
    generation.list_count = text_list_count;
    generation.list_put = text_list_put;
    generation.font = text_font;
    generation.font_width = text_width;
    generation.font_height = text_height;
    generation.font_maximum = text_maximum;
    generation.release = text_release;
    status = mm_text_build_matrices(&text, body->actor.state.action, &generation) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
done:
    free(text.metrics.widths);
    free(text.metrics.heights);
    mm_session_archive_close(&font_archive);
    return status;
}

int mm_session_body_parts(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body)
{
    return in->text ? text_parts(in, body) : mm_session_body_cel(in, body);
}
