#include "movie_session_internal.h"
#include "mm/font_resource.h"
#include "mm/string_wire.h"
#include "mm/template_runtime.h"
#include "mm/text_generation.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

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

typedef struct MovieBodyShape
{
    const MMPartIndexView *parents, *sets;
    MMMovieEvaluatedBody *body;
} MovieBodyShape;

static int read_body_part(void *context, MMI32 index, MMI32 *set)
{
    const MovieBodyShape *shape = (const MovieBodyShape *)context;
    MMMovieBodyPart *part = &shape->body->parts[index];
    part->parent = mm_part_index_get(shape->parents, index);
    part->set = mm_part_index_get(shape->sets, index);
    if (part->set < 0)
        return MM_MOVIE_BAD_FILE;
    *set = part->set;
    return MM_MOVIE_OK;
}

int mm_session_body_shape(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body, const MMActorHeader *actor)
{
    MMMovieTemplate *tmpl = in->tmpl;
    MMTemplateRead reader = mm_session_template_reader(tmpl);
    MMTemplateParts parts = {NULL, 0, read_body_part};
    MovieBodyShape shape;
    int status = mm_template_read_shape(&tmpl->state, &reader);
    if (status != MM_MOVIE_OK)
        return status;
    shape.parents = &tmpl->state.parents->view;
    shape.sets = &tmpl->state.sets->view;
    shape.body = body;
    if (shape.parents->count != shape.sets->count)
        return MM_MOVIE_BAD_FILE;
    status = reserve_parts(body, (MMU32)shape.parents->count);
    if (status != MM_MOVIE_OK)
        return status;
    body->model_owner.source = actor->source_id;
    body->model_owner.type = actor->template_type;
    body->model_owner.number = actor->template_number;
    parts.context = &shape;
    parts.count = shape.parents->count;
    return mm_template_count_sets(&tmpl->state, &parts);
}

typedef struct MovieBodyCel
{
    const MMMovieBodyInput *input;
    MMI32 source;
    MMChunkAccess access;
} MovieBodyCel;

int mm_movie_cel_part(const MMMovieCelRead *read, MMU32 ordinal, MMMovieCelPart *out)
{
    MMMovieCelPart binding;
    MMI32 model, matrix;
    int status;
    memset(&binding.model, 0, sizeof(binding.model));
    read->indices(read->context, ordinal, &model, &matrix);
    if (model < MM_TEMPLATE_KEEP_MODEL || matrix < 0)
        return MM_MOVIE_BAD_FILE;
    if (model != MM_TEMPLATE_KEEP_MODEL)
    {
        status = read->model(read->context, ordinal, (MMU32)model, &binding.model);
        if (status != MM_MOVIE_OK)
            return status;
        if (!binding.model.type)
            return MM_MOVIE_BAD_FILE;
    }
    status = read->matrix(read->context, matrix, &binding.matrix);
    if (status == MM_MOVIE_OK)
        *out = binding;
    return status;
}

int mm_movie_cel_model(const MMChunkAccess *access, MMU32 position, MMU32 slot, MMI32 source, MMMovieAsset *out)
{
    MMChunkEntry entry;
    int status = body_child(access, position, slot, MM_TEMPLATE_BMDL, &entry);
    if (status == MM_MOVIE_OK)
    {
        out->source = source;
        out->type = entry.type;
        out->number = entry.number;
    }
    return status;
}

static void movie_cel_indices(void *context, MMU32 ordinal, MMI32 *model, MMI32 *matrix)
{
    const MovieBodyCel *cel = (const MovieBodyCel *)context;
    const MMActionDefinition *action = &cel->input->action->definition;
    unsigned short indices[2];
    if (!mm_action_get_part(action, cel->input->cel, (MMI32)ordinal, indices))
    {
        *model = -1;
        *matrix = -1;
        return;
    }
    *model = indices[0] < 32768 ? (MMI32)indices[0] : (MMI32)indices[0] - 65536L;
    *matrix = indices[1] < 32768 && (MMU32)indices[1] < action->matrices->header.count ? (MMI32)indices[1] : -1;
}

static int movie_cel_model(void *context, MMU32 ordinal, MMU32 slot, MMMovieAsset *out)
{
    const MovieBodyCel *cel = (const MovieBodyCel *)context;
    (void)ordinal;
    return mm_movie_cel_model(&cel->access, cel->input->tmpl->position, slot, cel->source, out);
}

static int movie_cel_matrix(void *context, MMI32 matrix, MMActorMatrix *out)
{
    const MovieBodyCel *cel = (const MovieBodyCel *)context;
    return mm_action_get_matrix(&cel->input->action->definition, matrix, out) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

int mm_session_body_cel(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body)
{
    MovieBodyCel cel;
    MMMovieCelRead read;
    MMMovieCelPart binding;
    MMU32 i;
    MMI32 parts = mm_action_part_count(&in->action->definition, in->cel);
    int status;
    if (parts < 0 || body->part_count > (MMU32)parts)
        return MM_MOVIE_BAD_FILE;
    cel.input = in;
    cel.source = body->model_owner.source;
    cel.access = in->tmpl->archive.access;
    status = mm_session_action_matrices(in->action);
    if (status != MM_MOVIE_OK)
        return status;
    read.context = &cel;
    read.indices = movie_cel_indices;
    read.model = movie_cel_model;
    read.matrix = movie_cel_matrix;
    for (i = 0; i < body->part_count; ++i)
    {
        status = mm_movie_cel_part(&read, i, &binding);
        if (status != MM_MOVIE_OK)
            break;
        body->parts[i].matrix = binding.matrix;
        body->parts[i].model = binding.model;
        if (binding.model.type)
            body->parts[i].active_model = binding.model;
    }
    return status;
}

typedef struct TextBody
{
    unsigned char string[MM_STRING_MAX + 2];
    MMFontResource metrics;
    MMI32 shape;
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
    int status = mm_session_name(&in->tmpl->archive, in->tmpl->position, &name);
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
static void text_write_matrix(void *p, MMI32 i, const MMActorMatrix *value)
{
    MMMovieEvaluatedBody *body = (MMMovieEvaluatedBody *)p;
    body->parts[i].matrix = *value;
}
static void text_release(void *p, void *value) { (void)p; (void)value; }

static int text_parts(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body)
{
    const unsigned short one = 1;
    const MMChunkAccess *access = &in->tmpl->archive.access;
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
    status = text_name(in, &text);
    if (status != MM_MOVIE_OK)
        return status;
    status = body_child(access, in->tmpl->position, 0, MM_TDT, &entry);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_session_payload(&in->tmpl->archive, &entry, 0, &payload);
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
    generation.font = text_font;
    generation.font_width = text_width;
    generation.font_height = text_height;
    generation.font_maximum = text_maximum;
    generation.release = text_release;
    status = mm_text_evaluate_cel(&text, body->actor.state.action, in->cel, text_write_matrix, body, &generation)
             ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
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
