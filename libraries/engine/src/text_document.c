#include "mm/text_document.h"
static void drop(int slot, const MMTextDocumentObjects *o)
{
    void *p = o->get(o->context, slot);
    if (p)
    {
        o->release(o->context, slot, p);
        o->set(o->context, slot, 0);
    }
}
void mm_text_document_dispose(int rich, const MMTextDocumentObjects *o)
{
    if (rich)
    {
        drop(MM_TEXT_OWNER_PROPERTIES, o);
        drop(MM_TEXT_OWNER_ARGUMENTS, o);
        drop(MM_TEXT_OWNER_ARCHIVE, o);
    }
    else
    {
        drop(MM_TEXT_OWNER_STREAM, o);
        drop(MM_TEXT_OWNER_FILE, o);
    }
}
int mm_text_base_load(int encoding, const MMTextBaseServices *s)
{
    const MMTextDocumentObjects *o = &s->objects;
    MMTextLocation location;
    MMPieceTable *pieces;
    MMFilePiece piece;
    int r;
    location.file = o->get(o->context, MM_TEXT_OWNER_FILE);
    if (!location.file)
        return 1;
    location.bytes = s->file_bytes(s->context, location.file);
    if (!location.bytes)
        return 1;
    location.offset = 0;
    o->retain(o->context, MM_TEXT_OWNER_FILE, location.file);
    r = s->translate(s->context, &location, encoding);
    if (r > 0 && location.bytes)
    {
        pieces = s->pieces(o->get(o->context, MM_TEXT_OWNER_STREAM));
        piece.file = location.file;
        piece.offset = location.offset;
        piece.length = (MMU32)location.bytes;
        r = mm_piece_table_replace_file(pieces, 0, pieces->length, &piece, 0) == MM_DATA_OK;
    }
    if (location.file)
        o->release(o->context, MM_TEXT_OWNER_FILE, location.file);
    return r;
}
int mm_text_base_init(const MMTextDocumentConfig *c, void *name, void *stream, int encoding,
                      const MMTextBaseServices *s)
{
    const MMTextDocumentObjects *o = &s->objects;
    MMPieceTable *pieces;
    void *owner = 0;
    int r;
    unsigned char cr[2];
    if (c->unit != 1 && c->unit != 2)
        return 0;
    if (name)
    {
        r = s->open(s->context, name, &owner);
        o->set(o->context, MM_TEXT_OWNER_FILE, owner);
        if (r <= 0)
            return r;
    }
    if (stream)
    {
        if (encoding != c->system)
        {
            s->diagnostic(s->context, MM_TEXT_DOC_ENCODING);
            return 0;
        }
        if (s->pieces(stream)->length % c->unit)
        {
            s->diagnostic(s->context, MM_TEXT_DOC_PARTIAL_CHAR);
            return 0;
        }
        o->retain(o->context, MM_TEXT_OWNER_STREAM, stream);
        o->set(o->context, MM_TEXT_OWNER_STREAM, stream);
    }
    else
    {
        owner = 0;
        r = s->new_stream(s->context, &owner);
        o->set(o->context, MM_TEXT_OWNER_STREAM, owner);
        if (r <= 0)
            return r;
        r = s->load(s->context, encoding);
        if (r <= 0)
            return r;
    }
    /* A virtual load hook may replace the published stream. */
    pieces = s->pieces(o->get(o->context, MM_TEXT_OWNER_STREAM));
    cr[0] = 13;
    cr[1] = 0;
    if (c->unit == 2 && c->big)
    {
        cr[0] = 0;
        cr[1] = 13;
    }
    return mm_piece_table_replace(pieces, pieces->length, 0, cr, c->unit) == MM_DATA_OK;
}
int mm_text_rich_init(const MMTextDocumentConfig *c, void *name, MMU32 type, const MMTextRichInitServices *s)
{
    const MMTextDocumentObjects *o = &s->objects;
    void *owner = 0;
    MMTextChunk chunk;
    int r;
    if (name)
    {
        r = s->open(s->context, name, &owner);
        if (r > 0)
        {
            r = s->first(s->context, owner, type, &chunk);
            if (r > 0)
                r = s->read(s->context, owner, &chunk);
        }
        if (r <= 0)
        {
            if (owner)
                o->release(o->context, MM_TEXT_OWNER_ARCHIVE, owner);
            if (!r)
                s->error(s->context, MM_TEXT_DOC_READ);
            return r;
        }
        o->set(o->context, MM_TEXT_OWNER_ARCHIVE, owner);
        return 1;
    }
    r = s->base(s->context);
    if (r <= 0)
        return r;
    r = s->properties(s->context, 8, &owner);
    o->set(o->context, MM_TEXT_OWNER_PROPERTIES, owner);
    if (r <= 0)
        return r;
    s->min_grow(s->context, owner, 10);
    return s->font(s->context, c->system, 12);
}
int mm_text_document_create(void **out, int read_error, const MMTextFactoryServices *s)
{
    void *p = 0;
    int r;
    *out = 0;
    r = s->create(s->context, &p);
    if (r > 0)
    {
        r = s->init(s->context, p);
        if (!r && read_error)
            s->read_error(s->context);
    }
    if (r <= 0)
    {
        if (p)
            s->release(s->context, p);
        return r;
    }
    *out = p;
    return 1;
}
static int slot(int profile)
{
    return profile == MM_TEXT_RICH ? MM_TEXT_OWNER_ARCHIVE : MM_TEXT_OWNER_FILE;
}
int mm_text_document_name(int profile, void *name, const MMTextFileServices *s)
{
    void *owner = s->objects.get(s->objects.context, slot(profile));
    if (!owner || (profile == MM_TEXT_PLAIN && s->is_temp(s->context, profile, owner)))
        return 0;
    return s->name(s->context, profile, owner, name);
}
static int save_plain(const MMTextDocumentConfig *c, void *temporary, int adopt, const MMTextFileServices *s)
{
    const MMTextDocumentObjects *o = &s->objects;
    MMPieceTable *pieces = s->pieces(o->get(o->context, MM_TEXT_OWNER_STREAM));
    MMOwnedBlock block;
    MMBlockFile location;
    MMFilePiece piece;
    MMBlockIO io;
    unsigned char bom[2];
    int ok = 1;
    if (pieces->length < c->unit || mm_owned_block_init(&block, s->handles, s->files) != MM_DATA_OK)
        return 0;
    location.file = temporary;
    location.offset = 0;
    location.length = c->unit == 2 ? pieces->length : pieces->length - c->unit;
    if (mm_owned_block_set_file(&block, &location, 0) != MM_DATA_OK)
        return 0;
    if (c->unit == 2)
    {
        bom[c->big ? 1 : 0] = 0xff;
        bom[c->big ? 0 : 1] = 0xfe;
        ok = mm_owned_block_io(&block, &io) == MM_DATA_OK && mm_block_write(&io, 0, bom, 2, 0) == MM_DATA_OK &&
             mm_block_move_min(&block.view, 2) == MM_DATA_OK;
    }
    if (ok)
        ok = mm_piece_table_write(pieces, 0, &block, s->copy_allocator) == MM_DATA_OK;
    piece.file = temporary;
    piece.offset = block.view.start;
    piece.length = block.view.length;
    mm_owned_block_clear(&block);
    if (ok && adopt)
        mm_piece_table_replace_file(pieces, 0, piece.length, &piece, 0);
    return ok;
}
int mm_text_document_save(int profile, const MMTextDocumentConfig *c, void *name, void *fallback, int adopt,
                          const MMTextFileServices *s)
{
    const MMTextDocumentObjects *o = &s->objects;
    void *current, *temporary = 0;
    int r, kind = slot(profile);
    if (c->unit != 1 && c->unit != 2)
        return 0;
    if (!name)
    {
        current = o->get(o->context, kind);
        if (!current)
        {
            s->diagnostic(s->context, MM_TEXT_DOC_NO_FILE);
            return 0;
        }
        r = s->name(s->context, profile, current, fallback);
        if (r <= 0)
            return r;
        name = fallback;
        adopt = 1;
    }
    r = s->create(s->context, profile, name, &temporary);
    if (r <= 0)
        goto fail;
    if (profile == MM_TEXT_RICH)
    {
        r = s->chunk(s->context, temporary, adopt);
        if (r <= 0)
            goto fail;
        r = s->save(s->context, temporary);
        if (r <= 0)
            goto fail;
    }
    else
    {
        r = save_plain(c, temporary, adopt, s);
        if (!r)
            goto fail;
    }
    r = s->rename(s->context, profile, temporary, name);
    if (r <= 0)
        goto fail;
    s->keep(s->context, profile, temporary);
    if (adopt)
    {
        drop(kind, o);
        o->set(o->context, kind, temporary);
        s->clean(s->context);
    }
    else
        o->release(o->context, kind, temporary);
    return 1;
fail:
    if (temporary)
        o->release(o->context, kind, temporary);
    if (!r && profile == MM_TEXT_RICH)
        s->error(s->context, MM_TEXT_DOC_SAVE);
    return r;
}
