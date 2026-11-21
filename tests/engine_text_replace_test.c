#define MM_TEXT_FORMAT_NO_LATE_INPUT
#include "text_format_test_host.h"
#include "mm/text_replace.h"
typedef struct Token
{
    int refs;
} Token;
typedef struct Replacement
{
    Host host;
    MMTextReplaceRichServices s;
    Token file, translated, temporary;
    MMPieceTable stream, source;
    MMI32 characters, translated_bytes;
    MMI32 undo_first[4], undo_end[4], undo_inserted[4], notify_at[4], notify_inserted[4], notify_deleted[4];
    int notify_file_refs[4], notify_translated_refs[4], undos, notifies, adjusts, copy, change_owner, diagnostics[4],
        too_much;
    MMU32 notify_flags[4];
    MMTextReplaceRequest forwarded;
} Replacement;
static const char initial[] = "0123456789ABCDEFGH";
static const char source[] = "STUVWXYZ";
static void *r_owner(void *v, int slot)
{
    Replacement *h = (Replacement *)v;
    CHECK(slot == MM_TEXT_OWNER_STREAM);
    return &h->stream;
}
static void r_retain(void *v, int kind, void *p)
{
    Replacement *h = (Replacement *)v;
    Token *t = (Token *)p;
    CHECK(kind == MM_TEXT_OWNER_FILE && t->refs > 0);
    ++t->refs;
    event(&h->host, '+');
}
static void r_drop(void *v, int kind, void *p)
{
    Replacement *h = (Replacement *)v;
    Token *t = (Token *)p;
    CHECK(kind == MM_TEXT_OWNER_FILE && t->refs > 0);
    --t->refs;
    event(&h->host, t == &h->file ? 'f' : 't');
}
static int r_translate(void *v, MMTextLocation *p, int system)
{
    Replacement *h = (Replacement *)v;
    int r;
    CHECK(system == h->host.config.text.system || system == 0x1111);
    r = effect(&h->host, 'T');
    if (h->change_owner && p->file == &h->file)
    {
        r_drop(h, MM_TEXT_OWNER_FILE, p->file);
        p->file = &h->translated;
        ++h->translated.refs;
        p->offset = 0;
    }
    if (h->translated_bytes >= 0)
        p->bytes = h->translated_bytes;
    return r;
}
static MMPieceTable *r_pieces(void *p)
{
    return (MMPieceTable *)p;
}
static int r_read(void *v, const MMFilePiece *p, void *out)
{
    Replacement *h = (Replacement *)v;
    CHECK((p->file == &h->file && p->offset == 7) || (p->file == &h->translated && !p->offset));
    memset(out, p->file == &h->file ? 'F' : 'T', p->length);
    return effect(&h->host, 'l') > 0;
}
static void r_retain_piece(void *v, void *p)
{
    r_retain(v, MM_TEXT_OWNER_FILE, p);
}
static void r_drop_piece(void *v, void *p)
{
    r_drop(v, MM_TEXT_OWNER_FILE, p);
}
static void *r_temp(void *v)
{
    Replacement *h = (Replacement *)v;
    CHECK(!h->temporary.refs);
    h->temporary.refs = 1;
    return &h->temporary;
}
static int r_copy(void *v, const MMFilePiece *from, const MMFilePiece *to)
{
    Replacement *h = (Replacement *)v;
    CHECK((from->file == &h->file && from->offset == 7) || (from->file == &h->translated && !from->offset));
    CHECK(to->file == &h->temporary && !to->offset && to->length == from->length);
    ++h->copy;
    return effect(&h->host, 'l') > 0;
}
static int r_invalidate(void *v, MMI32 at, MMI32 ins, MMI32 del)
{
    Replacement *h = (Replacement *)v;
    CHECK(at >= 0 && ins >= 0 && del >= 0);
    h->characters = h->stream.length / h->host.config.text.unit;
    return effect(&h->host, 'i');
}
static void r_validate(void *v)
{
    event(&((Replacement *)v)->host, 'v');
}
static int r_notify(void *v, MMI32 at, MMI32 ins, MMI32 del, MMU32 flags)
{
    Replacement *h = (Replacement *)v;
    int i = h->notifies++;
    CHECK(i < 4);
    h->notify_at[i] = at;
    h->notify_inserted[i] = ins;
    h->notify_deleted[i] = del;
    h->notify_flags[i] = flags;
    h->notify_file_refs[i] = h->file.refs;
    h->notify_translated_refs[i] = h->translated.refs;
    h->host.dirty = 1;
    return effect(&h->host, 'n');
}
static MMI32 r_characters(void *v)
{
    return ((Replacement *)v)->characters;
}
static int r_undo(void *v, MMI32 first, MMI32 end, MMI32 inserted)
{
    Replacement *h = (Replacement *)v;
    int i = h->undos++;
    CHECK(i < 4);
    h->undo_first[i] = first;
    h->undo_end[i] = end;
    h->undo_inserted[i] = inserted;
    return effect(&h->host, 'U');
}
static int r_cancel(void *v)
{
    return effect(&((Replacement *)v)->host, 'C');
}
static int r_commit(void *v)
{
    return effect(&((Replacement *)v)->host, 'M');
}
static int r_reserve(void *v, MMI32 count)
{
    Replacement *h = (Replacement *)v;
    CHECK(count >= 0 && count <= 14);
    return effect(&h->host, 'R');
}
static int r_base(void *v, const MMTextReplaceRequest *q)
{
    Replacement *h = (Replacement *)v;
    int r = effect(&h->host, 'B');
    if (r > 0)
        r = mm_text_replace_base(h->host.config.text.unit, q, &h->s.base);
    return r;
}
static int r_virtual(void *v, const MMTextReplaceRequest *q)
{
    Replacement *h = (Replacement *)v;
    int r = effect(&h->host, 'D');
    h->forwarded = *q;
    if (r > 0)
        r = mm_text_replace_rich(h->host.config.text.unit, h->host.config.text.system, q, &h->s);
    return r;
}
static int r_adjust(void *v, MMI32 at, MMI32 ins, MMI32 del)
{
    Replacement *h = (Replacement *)v;
    CHECK(at >= 0 && ins >= 0 && del >= 0);
    ++h->adjusts;
    return effect(&h->host, 'j');
}
static void r_too_much(void *v)
{
    Replacement *h = (Replacement *)v;
    ++h->too_much;
    event(&h->host, 'Z');
}
static void r_diagnostic(void *v, int kind)
{
    Replacement *h = (Replacement *)v;
    CHECK(kind >= 0 && kind < 4);
    ++h->diagnostics[kind];
    event(&h->host, 'x');
}
static void r_input(void *v, int paragraph, MMTextFormatConfig *c, void *value, void *old, int *has_old)
{
    Replacement *h = (Replacement *)v;
    MMTextParagraph p;
    (void)old;
    *c = h->host.config;
    *has_old = 0;
    event(&h->host, paragraph ? 'p' : 'c');
    if (paragraph)
    {
        memset(&p, 0, sizeof(p));
        p.justification = 1;
        p.indent = 2;
        p.tab = 20;
        p.line = 300;
        p.after = 256;
        *(MMTextParagraph *)value = p;
    }
    else
        *(MMTextCharacter *)value = h->host.input;
}
static void r_init(Replacement *h, unsigned unit)
{
    MMTextReplaceBaseServices *b;
    MMPieceFiles files;
    memset(h, 0, sizeof(*h));
    init(&h->host, unit, 0);
    h->characters = 9;
    h->file.refs = 1;
    h->translated_bytes = -1;
    memset(&files, 0, sizeof(files));
    files.context = h;
    files.retain = r_retain_piece;
    files.release = r_drop_piece;
    files.create_temp = r_temp;
    files.read = r_read;
    files.copy = r_copy;
    CHECK(mm_piece_table_init(&h->stream, NULL, &files, 0) == MM_DATA_OK);
    CHECK(mm_piece_table_init(&h->source, NULL, &files, 0) == MM_DATA_OK);
    CHECK(mm_piece_table_replace(&h->stream, 0, 0, initial, 9 * unit) == MM_DATA_OK);
    CHECK(mm_piece_table_replace(&h->source, 0, 0, source, sizeof(source) - 1) == MM_DATA_OK);
    h->s.context = h;
    b = &h->s.base;
    b->context = h;
    b->objects.context = h;
    b->objects.get = r_owner;
    b->objects.retain = r_retain;
    b->objects.release = r_drop;
    b->translate = r_translate;
    b->pieces = r_pieces;
    b->invalidate = r_invalidate;
    b->validate = r_validate;
    b->notify = r_notify;
    h->s.edit = h->host.edit;
    h->s.edit.context = h;
    h->s.edit.undo = r_undo;
    h->s.edit.cancel = r_cancel;
    h->s.edit.commit = r_commit;
    h->s.edit.reserve = r_reserve;
    h->s.edit.validate = r_validate;
    h->s.edit.read_input = r_input;
    h->s.characters = r_characters;
    h->s.replace_base = r_base;
    h->s.replace_virtual = r_virtual;
    h->s.adjust = r_adjust;
    h->s.too_much = r_too_much;
    h->s.diagnostic = r_diagnostic;
    h->s.notify = r_notify;
}
static MMTextReplaceRequest request(Replacement *h, int kind)
{
    static const unsigned char raw[8] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    MMTextReplaceRequest q;
    memset(&q, 0, sizeof(q));
    q.kind = kind;
    q.data = raw;
    q.inserted = 2;
    q.at = 2;
    q.deleted = 1;
    q.flags = 7;
    q.copy = 1;
    q.system = h->host.config.text.system;
    q.source = 1;
    q.stream = &h->source;
    q.location.file = &h->file;
    q.location.offset = 7;
    q.location.bytes = 4 * h->host.config.text.unit;
    return q;
}
static int simple(Replacement *h, const MMTextReplaceRequest *q)
{
    return mm_text_replace_rich(h->host.config.text.unit, h->host.config.text.system, q, &h->s);
}
static int formatted(Replacement *h, const MMTextReplaceRequest *q)
{
    return mm_text_replace_formatted(h->host.config.text.unit, h->host.config.text.system, q, &h->s);
}
static void released(Replacement *h)
{
    CHECK(mm_piece_table_dispose(&h->stream) == MM_DATA_OK);
    CHECK(mm_piece_table_dispose(&h->source) == MM_DATA_OK);
    CHECK(h->file.refs == 1 && !h->translated.refs && !h->temporary.refs);
}
static void fail_edit(Replacement *h, MMTextReplaceRequest *q)
{
    if (q->kind == MM_TEXT_REPLACE_LOCATION)
        h->host.fail = 'l';
    else if (q->kind == MM_TEXT_REPLACE_STREAM)
        q->source = 99;
    else
        q->at = 99;
}
static void base_cases(void)
{
    Replacement h;
    MMTextReplaceRequest q;
    unsigned unit, first, added;
    char bytes[32], replacement[12];
    int kind, change, failure, i, expected, r;
    for (unit = 1; unit <= 2; ++unit)
        for (kind = 0; kind < 3; ++kind)
            for (change = 0; change < 2; ++change)
            {
                r_init(&h, unit);
                q = request(&h, kind);
                h.change_owner = change;
                if (change)
                    h.translated_bytes = 6 * unit;
                CHECK(mm_text_replace_base(unit, &q, &h.s.base) == 1);
                expected = kind == MM_TEXT_REPLACE_LOCATION ? (change ? 6 : 4) : 2;
                first = 2 * unit;
                added = expected * unit;
                CHECK(h.stream.length == 8 * unit + added);
                CHECK(mm_piece_table_read(&h.stream, 0, bytes, h.stream.length) == MM_DATA_OK);
                CHECK(!memcmp(bytes, initial, first));
                if (kind == MM_TEXT_REPLACE_STREAM)
                    CHECK(!memcmp(bytes + first, source + unit, added));
                else if (kind == MM_TEXT_REPLACE_BYTES)
                    CHECK(!memcmp(bytes + first, q.data, added));
                else
                {
                    memset(replacement, change ? 'T' : 'F', added);
                    CHECK(!memcmp(bytes + first, replacement, added));
                }
                CHECK(!memcmp(bytes + first + added, initial + 3 * unit, 6 * unit));
                CHECK(h.notifies == 1 && h.notify_inserted[0] == expected && h.notify_flags[0] == 7 && h.host.dirty);
                if (kind == MM_TEXT_REPLACE_LOCATION)
                    CHECK(change ? h.notify_translated_refs[0] == 1 : h.notify_file_refs[0] == 2);
                released(&h);
                for (i = 0; i < 4; ++i)
                    for (failure = 0; failure >= -2; --failure)
                    {
                        if ((!i && failure) || (i == 3 && kind != MM_TEXT_REPLACE_LOCATION))
                            continue;
                        r_init(&h, unit);
                        q = request(&h, kind);
                        h.change_owner = change;
                        h.host.result = failure;
                        if (!i)
                            fail_edit(&h, &q);
                        else
                            h.host.fail = " inT"[i];
                        r = mm_text_replace_base(unit, &q, &h.s.base);
                        CHECK(r == failure);
                        if (!i)
                        {
                            CHECK(h.stream.length == 9 * unit && !h.notifies);
                            CHECK(mm_piece_table_read(&h.stream, 0, bytes, h.stream.length) == MM_DATA_OK);
                            CHECK(!memcmp(bytes, initial, h.stream.length));
                        }
                        released(&h);
                        if (i == 2)
                            CHECK(h.host.dirty);
                        else
                            CHECK(!h.host.dirty);
                    }
            }
}
static void rich_cases(void)
{
    Replacement h;
    MMTextReplaceRequest q;
    unsigned unit;
    int kind, change, failure, i, r;
    for (unit = 1; unit <= 2; ++unit)
        for (kind = 0; kind < 3; ++kind)
            for (change = 0; change < 2; ++change)
            {
                r_init(&h, unit);
                q = request(&h, kind);
                h.change_owner = change;
                if (change)
                    h.translated_bytes = 6 * unit;
                CHECK(simple(&h, &q) == 1);
                released(&h);
                CHECK(h.undos == 1 && h.adjusts == 1 && h.notifies == 2 && h.notify_flags[0] == 0 &&
                      h.notify_flags[1] == 7);
                CHECK(h.undo_inserted[0] == (kind == 1 ? (change ? 6 : 4) : 2));
                if (kind == 1)
                    CHECK(change ? (h.notify_translated_refs[0] == 2 && !h.notify_translated_refs[1])
                                 : (h.notify_file_refs[0] == 3 && h.notify_file_refs[1] == 1));
                for (i = 0; i < 8; ++i)
                    for (failure = 0; failure >= -2; --failure)
                    {
                        if ((i == 2 && failure) || (i == 7 && kind != MM_TEXT_REPLACE_LOCATION))
                            continue;
                        r_init(&h, unit);
                        q = request(&h, kind);
                        h.change_owner = change;
                        if (i == 2)
                            fail_edit(&h, &q);
                        else
                            h.host.fail = "UB ijnMT"[i];
                        h.host.result = failure;
                        r = simple(&h, &q);
                        CHECK(r == failure);
                        released(&h);
                        if (!failure && (i == 1 || i == 2 || i == 3 || i == 5))
                            CHECK(strchr(h.host.log, 'C'));
                        if (failure < 0 || i == 0 || i == 4 || i >= 6)
                            CHECK(!strchr(h.host.log, 'C'));
                    }
                r_init(&h, unit);
                q = request(&h, kind);
                h.characters = 0x00800000L - 2;
                CHECK(simple(&h, &q) == 0 && h.too_much == 1 && !h.undos && !h.notifies);
                released(&h);
            }
}
static void formatted_cases(void)
{
    Replacement h;
    MMTextReplaceRequest q;
    unsigned unit;
    int i, failure, r;
    for (unit = 1; unit <= 2; ++unit)
    {
        r_init(&h, unit);
        q = request(&h, 0);
        q.source = 0;
        h.s.has_character = 1;
        CHECK(formatted(&h, &q) == 1 && h.undos == 2 && h.notifies == 3 && h.adjusts == 1 && h.host.count == 8 &&
              h.host.fixed == 0x02000001UL);
        CHECK(h.notify_flags[0] == 0 && h.notify_flags[1] == 0 && h.notify_flags[2] == 7 && h.undo_inserted[0] == 2 &&
              h.undo_inserted[1] == 2);
        CHECK(strstr(h.host.log, "cFGpARUDUBivnjMvnk") != 0);
        released(&h);
        for (i = 0; i < 8; ++i)
            for (failure = 0; failure >= -2; --failure)
            {
                if (!failure && i >= 6)
                    continue;
                r_init(&h, unit);
                q = request(&h, 0);
                q.source = 0;
                h.s.has_character = 1;
                h.host.fail = "FGARUDXM"[i];
                h.host.result = failure;
                r = formatted(&h, &q);
                CHECK(r == failure);
                released(&h);
                if (!failure && i <= 4)
                {
                    CHECK(!strchr(h.host.log, 'C'));
                    if (i >= 2)
                        CHECK(h.host.free || !h.host.length);
                }
                if (!failure && i == 5)
                    CHECK(strchr(h.host.log, 'C') && h.host.free);
                if (failure < 0)
                    CHECK(!strchr(h.host.log, 'C'));
            }
        r_init(&h, unit);
        q = request(&h, 0);
        q.source = 0;
        h.s.has_character = h.s.has_paragraph = 1;
        q.at = 0;
        q.deleted = 6;
        q.inserted = 3;
        h.host.is_start = 1;
        CHECK(formatted(&h, &q) == 1 && h.undo_first[0] == 0 && h.undo_end[0] == 6 && h.undo_inserted[0] == 3 &&
              h.host.count == 12);
        CHECK(h.notify_at[2] == 0 && h.notify_inserted[2] == 3 && h.notify_deleted[2] == 6);
        released(&h);
        r_init(&h, unit);
        q = request(&h, 0);
        q.source = 0;
        q.inserted = 0;
        h.s.has_character = h.s.has_paragraph = 1;
        CHECK(formatted(&h, &q) == 1 && !strchr(h.host.log, 'F') && h.host.count == 0 && h.undos == 2);
        released(&h);
        r_init(&h, unit);
        q = request(&h, 0);
        q.source = 0;
        h.s.has_character = 1;
        h.characters = 0x00800000L - 2;
        CHECK(formatted(&h, &q) == 0 && h.too_much == 1 && !strchr(h.host.log, 'c') && !h.undos);
        released(&h);
    }
}
static void formatted_locations(void)
{
    Replacement h;
    MMTextReplaceRequest q;
    unsigned unit;
    int change, failure;
    for (unit = 1; unit <= 2; ++unit)
        for (change = 0; change < 2; ++change)
        {
            r_init(&h, unit);
            q = request(&h, 1);
            q.source = 0;
            h.change_owner = change;
            h.translated_bytes = change ? 6 * unit : 4 * unit;
            CHECK(mm_text_replace_formatted_location(unit, h.host.config.text.system, &q, &h.s) == 1);
            released(&h);
            CHECK(h.undos == 2 && h.notifies == 3 && h.forwarded.inserted == 4 * (MMI32)unit &&
                  h.forwarded.location.bytes == h.translated_bytes && h.forwarded.copy == !change);
            CHECK(h.undo_inserted[0] == 4 * (MMI32)unit && h.undo_inserted[1] == h.translated_bytes / (MMI32)unit);
            CHECK(h.diagnostics[MM_TEXT_REPLACE_BAD_LOCATION] == (4 * (MMI32)unit * (MMI32)unit != h.translated_bytes));
            CHECK(h.notify_inserted[2] == 4 * (MMI32)unit &&
                  (change ? h.notify_translated_refs[0] == 3 : h.notify_file_refs[0] == 4));
            for (failure = 0; failure >= -2; --failure)
            {
                r_init(&h, unit);
                q = request(&h, 1);
                q.source = 0;
                h.change_owner = change;
                h.host.fail = 'T';
                h.host.result = failure;
                CHECK(mm_text_replace_formatted_location(unit, h.host.config.text.system, &q, &h.s) == failure &&
                      !h.undos);
                released(&h);
            }
        }
}
static void long_locations(void)
{
    Replacement h;
    MMTextReplaceRequest q;
    MMFilePiece piece;
    int change, mode, copies;
    for (mode = 0; mode < 3; ++mode)
        for (change = 0; change < 2; ++change)
        {
            r_init(&h, 2);
            q = request(&h, MM_TEXT_REPLACE_LOCATION);
            q.source = 0;
            q.location.bytes = 2048;
            h.change_owner = change;
            if (mode == 0)
                CHECK(mm_text_replace_base(2, &q, &h.s.base) == 1);
            else if (mode == 1)
                CHECK(simple(&h, &q) == 1);
            else
                CHECK(mm_text_replace_formatted_location(2, h.host.config.text.system, &q, &h.s) == 1);
            copies = !change || mode == 1;
            CHECK(h.copy == copies && h.stream.length == 2064);
            CHECK(mm_piece_table_get(&h.stream, 1, &piece, NULL) == MM_DATA_OK);
            CHECK(piece.file == (copies ? &h.temporary : &h.translated) && !piece.offset && piece.length == 2048);
            CHECK(h.file.refs == 1 && h.translated.refs == !copies && h.temporary.refs == copies);
            released(&h);
        }
}
int main(void)
{
    base_cases();
    rich_cases();
    formatted_cases();
    formatted_locations();
    long_locations();
    puts("Text replacement checks passed");
    return 0;
}
