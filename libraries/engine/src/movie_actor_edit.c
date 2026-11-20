#include "actor_archive_internal.h"
#include "mm/chunk_ops.h"
#include "mm/scene_data.h"
#include "mm/template_create.h"

#define ACTOR_TYPE 0x41435452UL
#define MATERIAL_TYPE 0x4d54524cUL

static int retail_movie(const MMActorAuthorHost *host, MMMovieArchiveRoot *root, MMI32 *source, int *detail)
{
    const MMMovieArchiveEditor *editor = host->destination;
    *detail = mm_movie_archive_ensure_root(editor, root);
    if (*detail != MM_MOVIE_SCENE_ARCHIVE_OK)
        return MM_ACTOR_AUTHOR_ROOT;
    *detail = mm_movie_archive_register_source(editor, host->text, root->number, root->created ? 1 : 2, NULL, source);
    return *detail == MM_MOVIE_SOURCE_EDIT_OK ? MM_ACTOR_AUTHOR_OK : MM_ACTOR_AUTHOR_SOURCE;
}

static int publish_actor(const MMMovieArchiveEditor *editor, MMU32 movie, const MMActorArchivePlacement *placement,
                         const MMActorHeader *header, MMU32 flags, const void *name, MMU32 characters, int *detail)
{
    MMActorArchiveName entry;
    entry.actor.actor_id = header->actor_id;
    entry.actor.reference_count = 1;
    entry.actor.source_id = header->source_id;
    entry.actor.browser_flags = flags;
    entry.actor.template_type = header->template_type;
    entry.actor.template_number = header->template_number;
    entry.text = name;
    entry.characters = characters;
    entry.replace_existing = placement == NULL;
    *detail = mm_actor_archive_register(editor, movie, placement, &entry);
    return *detail == MM_ACTOR_REGISTER_OK ? MM_ACTOR_AUTHOR_OK : MM_ACTOR_AUTHOR_REGISTER;
}

static int create_actor(const MMMovieArchiveEditor *editor, MMU32 number, const MMActorHeader *header)
{
    MMU32 assigned;
    if (!editor->add(editor->context, ACTOR_TYPE, sizeof(*header), &assigned, NULL) ||
        !editor->put(editor->context, ACTOR_TYPE, assigned, header, sizeof(*header)))
        return MM_ACTOR_AUTHOR_WRITE_ACTOR;
    if (assigned == number)
        return MM_ACTOR_AUTHOR_OK;
    if (mm_chunk_move(&editor->edit, ACTOR_TYPE, assigned, ACTOR_TYPE, number) != MM_CHUNK_OK)
        return MM_ACTOR_AUTHOR_ACTOR_NUMBER;
    return MM_ACTOR_AUTHOR_OK;
}

int mm_actor_archive_insert(const MMActorAuthorHost *host, const MMActorInsert *insert, int *detail)
{
    const MMMovieArchiveEditor *source = host->source, *editor = host->destination;
    MMSceneState scene;
    MMTemplateState tmpl;
    MMMovieArchiveRoot root;
    MMActorHeader header = {0};
    MMActorArchivePlacement placement;
    MMChunkChild text = {MM_TMPL, 0, 0};
    MMReadStream stream;
    unsigned char bytes[MM_TEMPLATE_HEADER_BYTES];
    unsigned short angles[3];
    MMU32 number, flags, i;
    MMI32 retail;
    int status;
    number = insert->template_number;
    if (!source->read(source->context, MM_TMPL, number, &stream) ||
        !mm_read_exact(&stream, 0, bytes, sizeof(bytes)))
        return MM_ACTOR_AUTHOR_READ_TEMPLATE;
    if (!mm_template_decode_header(bytes, source->big_endian, &tmpl) ||
        ((tmpl.flags & MM_TEMPLATE_TEXT) != 0) != (insert->embedded_text != 0))
        return MM_ACTOR_AUTHOR_TEMPLATE;
    if (!mm_scene_archive_read_header(editor, insert->scene, &scene) ||
        insert->first < scene.firstFrame || insert->first > scene.lastFrame)
        return MM_ACTOR_AUTHOR_FRAME;
    for (i = 0; i < 3; ++i)
    {
        float value = insert->point[i];
        if (!(value >= -32768.0f && value < 32768.0f))
            return MM_ACTOR_AUTHOR_POSITION;
        header.translation[i] = (MMU32)(MMI32)(value * 65536.0f);
    }
    status = retail_movie(host, &root, &retail, detail);
    if (status != MM_ACTOR_AUTHOR_OK)
        return status;
    if (insert->embedded_text)
    {
        if (!host->copy(host->context, MM_TMPL, insert->template_number, &number))
            return MM_ACTOR_AUTHOR_COPY_TEMPLATE;
        if (!mm_actor_archive_set_font_source(editor, number, retail))
            return MM_ACTOR_AUTHOR_FONT;
    }
    header.order = 1;
    header.system = (unsigned short)editor->system;
    header.actor_id = insert->actor_id;
    header.first_frame = insert->first;
    header.last_frame = insert->last;
    header.source_id = insert->embedded_text ? 0 : retail;
    header.template_type = MM_TMPL;
    header.template_number = number;
    status = create_actor(editor, insert->number, &header);
    if (status != MM_ACTOR_AUTHOR_OK)
        return status;
    text.number = number;
    if (insert->embedded_text && mm_chunk_adopt(&editor->edit, ACTOR_TYPE, insert->number, &text, 1) != MM_CHUNK_OK)
        return MM_ACTOR_AUTHOR_LINK_TEMPLATE;
    angles[0] = tmpl.x;
    angles[1] = tmpl.y;
    angles[2] = tmpl.z;
    if (!mm_actor_archive_write_stationary(editor, insert->number, insert->first, angles))
        return MM_ACTOR_AUTHOR_TRACK;
    placement.scene = insert->scene;
    placement.actor = insert->number;
    placement.first_frame = scene.firstFrame;
    placement.invalidate_thumbnail = insert->first == scene.firstFrame;
    flags = insert->embedded_text ? 2 : (tmpl.flags & MM_TEMPLATE_PROP) != 0;
    return publish_actor(editor, root.number, &placement, &header, flags, insert->name, insert->characters, detail);
}

static int replace_child(const MMMovieArchiveEditor *editor, MMU32 actor, MMU32 type, MMU32 number)
{
    MMChunkAccess chunks;
    MMChunkChild previous = {0}, replacement = {0};
    previous.type = replacement.type = type;
    replacement.number = number;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_link_number(&chunks, ACTOR_TYPE, actor, 0, type, &previous.number) == MM_CHUNK_OK)
        /* A copied asset is a loner until adoption, even if the copy reused this key. */
        mm_chunk_unlink(&editor->edit, ACTOR_TYPE, actor, &previous);
    return mm_chunk_adopt(&editor->edit, ACTOR_TYPE, actor, &replacement, 1) == MM_CHUNK_OK;
}

int mm_actor_archive_replace_text(const MMActorAuthorHost *host, const MMActorTextReplacement *request, int *detail)
{
    const MMMovieArchiveEditor *editor = host->destination;
    MMMovieArchiveRoot root;
    MMActorHeader header;
    MMU32 number, material;
    MMI32 retail;
    int status;
    if (!host->copy(host->context, MM_TMPL, 0, &number))
        return MM_ACTOR_AUTHOR_COPY_TEMPLATE;
    status = retail_movie(host, &root, &retail, detail);
    if (status != MM_ACTOR_AUTHOR_OK)
        return status;
    if (request->retail_font && !mm_actor_archive_set_font_source(editor, number, retail))
        return MM_ACTOR_AUTHOR_FONT;
    if (!mm_actor_archive_read_actor(editor, request->actor, &header))
        return MM_ACTOR_AUTHOR_READ_ACTOR;
    header.source_id = 0;
    header.wire_pointer_pad = 0;
    header.template_type = MM_TMPL;
    header.template_number = number;
    if (!editor->put(editor->context, ACTOR_TYPE, request->actor, &header, sizeof(header)))
        return MM_ACTOR_AUTHOR_WRITE_ACTOR;
    if (!replace_child(editor, request->actor, MM_TMPL, number))
        return MM_ACTOR_AUTHOR_LINK_TEMPLATE;
    if (request->custom_material)
    {
        if (!host->copy(host->context, MATERIAL_TYPE, 0, &material))
            return MM_ACTOR_AUTHOR_COPY_MATERIAL;
        if (!replace_child(editor, request->actor, MATERIAL_TYPE, material))
            return MM_ACTOR_AUTHOR_LINK_MATERIAL;
        *detail = mm_actor_archive_set_material(editor, request->actor, material);
        if (*detail != MM_ACTOR_EDIT_ARCHIVE_OK)
            return MM_ACTOR_AUTHOR_MATERIAL_EVENTS;
    }
    return publish_actor(editor, root.number, NULL, &header, 2, request->name, request->characters, detail);
}
