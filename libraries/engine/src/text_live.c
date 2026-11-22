#include "mm/text_live.h"
#include "mm/movie_archive_edit.h"

static int write_text(const MMArchiveWriter *archive, MMPieceTable *text, MMU32 root, MMU32 bytes,
                       const unsigned char marker[2], MMOwnedBlock *block)
{
    MMChunkChild child = {MM_TEXT_BYTES, 0, 0};
    MMBlockIO io;
    return mm_chunk_add_child_block(&archive->edit, MM_TEXT_ROOT, root, &child, bytes + 2, block) == MM_CHUNK_OK &&
           mm_owned_block_io(block, &io) == MM_DATA_OK &&
           mm_block_write(&io, 0, marker, 2, 0) == MM_DATA_OK &&
           mm_block_move_min(&block->view, 2) == MM_DATA_OK &&
           mm_piece_table_write(text, 0, block, archive->transfer_allocator) == MM_DATA_OK;
}

static int group(MMU32 root, MMU32 kind, MMTextCollection *p, int system, const MMRichTextWriteServices *s)
{
    int r = s->add_child(s->context, root, kind, mm_text_collection_bytes(p));
    return r > 0 ? s->group(s->context, p, 1, system) : r;
}
int mm_rich_text_save(const MMRichTextState *state, MMTextLiveOutput *out, int big, int redirect,
                      const MMRichTextLiveServices *s)
{
    const MMRichTextWriteServices *w = &s->write;
    const MMChunkPayloadServices *payloads = s->archive->payloads;
    MMTextChunk created;
    MMOwnedBlock text;
    MMFilePiece location;
    MMU32 bytes;
    unsigned char marker[2];
    int r;
    created.type = created.number = 0;
    out->chunk.type = MM_TEXT_ROOT;
    out->number_changed = 0;
    if (!s->text || s->text->length < state->font.unit ||
        mm_owned_block_init(&text, payloads->handles, payloads->files) != MM_DATA_OK)
    {
        s->error(s->context);
        return 0;
    }
    bytes = s->text->length - state->font.unit;
    r = mm_rich_text_create(&state->defaults, &state->font, &created, big, w);
    if (created.type == MM_TEXT_ROOT)
    {
        out->chunk.number = created.number;
        out->number_changed = 1;
    }
    if (r <= 0)
    {
        if (!r)
            s->error(s->context);
        return r;
    }
    marker[big ? 1 : 0] = (unsigned char)state->text_system;
    marker[big ? 0 : 1] = (unsigned char)((MMU32)state->text_system >> 8);
    r = write_text(s->archive, s->text, created.number, bytes, marker, &text);
    if (!r)
        goto fail;
    r = group(created.number, MM_TEXT_PROPERTIES, state->properties, state->text_system, w);
    if (r <= 0)
        goto fail;
    if (mm_text_collection_count(state->arguments))
    {
        r = group(created.number, MM_TEXT_ARGUMENTS, state->arguments, state->text_system, w);
        if (r <= 0)
            goto fail;
    }
    if (redirect)
    {
        location.file = text.file;
        location.offset = text.view.start;
        location.length = text.view.length;
        mm_piece_table_replace_file(s->text, 0, bytes, &location, 0);
    }
    mm_owned_block_clear(&text);
    return 1;
fail:
    mm_owned_block_clear(&text);
    w->remove(w->context, created.number);
    if (!r)
        s->error(s->context);
    return r;
}
int mm_text_box_store(MMU32 box, const MMTextBoxStoreServices *s)
{
    const MMChunkEdit *edit = &s->archive->edit;
    MMTextChunk chunk;
    MMChunkChild child;
    int r;
    s->state->child = 0;
    r = s->save(s->context, &chunk);
    if (r <= 0)
        return r;
    if (chunk.type != MM_TEXT_ROOT)
        s->assert_type(s->context);
    child.type = chunk.type;
    child.number = chunk.number;
    child.slot = 0;
    if (mm_chunk_adopt(edit, MM_TEXT_BOX, box, &child, 1) != MM_CHUNK_OK)
    {
        mm_chunk_delete(edit, child.type, child.number);
        return 0;
    }
    r = mm_text_box_write(s->state, box, s->archive);
    if (r <= 0)
    {
        mm_chunk_unlink(edit, MM_TEXT_BOX, box, &child);
        return r;
    }
    return 1;
}
