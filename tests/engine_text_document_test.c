#include "mm/text_document.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "text document failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Token
{
    int refs, temp, live;
    MMI32 bytes;
    unsigned char data[2052];
    MMPieceTable pieces;
} Token;
typedef struct Host
{
    MMTextBaseServices base;
    MMTextRichInitServices rich;
    MMTextFileServices file;
    MMTextFactoryServices factory;
    MMBlockFileServices block_files;
    MMTextDocumentConfig config;
    Token tokens[12];
    void *slots[5];
    int calls, fail_at, failure, partial, change_file, translated_empty, profile, dirty, redirected, redirect_flag,
        read_errors, save_errors, diagnostic;
    int names, requested, fallback, returned, load_replacement, saving, refuse_allocation;
    char failed, log[256];
    unsigned used;
} Host;
static void event(Host *h, char c)
{
    CHECK(h->used < 255);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int step(Host *h, char c)
{
    event(h, c);
    if (++h->calls == h->fail_at)
    {
        h->failed = c;
        return h->failure;
    }
    return 1;
}
static void *get(void *v, int slot)
{
    Host *h = (Host *)v;
    CHECK(slot >= 0 && slot < 5);
    return h->slots[slot];
}
static void set(void *v, int slot, void *p)
{
    Host *h = (Host *)v;
    CHECK(slot >= 0 && slot < 5);
    h->slots[slot] = p;
    event(h, (char)('0' + slot));
}
static void retain(void *v, int kind, void *p)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    (void)kind;
    CHECK(t->refs > 0);
    ++t->refs;
    event(h, '+');
}
static void release(void *v, int kind, void *p)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    CHECK(t->refs > 0);
    --t->refs;
    event(h, 'x');
    if (kind == MM_TEXT_OWNER_STREAM && !t->refs && t->live)
    {
        CHECK(mm_piece_table_dispose(&t->pieces) == MM_DATA_OK);
        t->live = 0;
    }
}
static void retain_file(void *v, void *p)
{
    retain(v, MM_TEXT_OWNER_FILE, p);
}
static void release_file(void *v, void *p)
{
    release(v, MM_TEXT_OWNER_FILE, p);
}
static void *unexpected_temp(void *v)
{
    (void)v;
    CHECK(0);
    return NULL;
}
static int read_file(void *v, void *p, MMU32 offset, void *out, size_t count)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    int redirect = h->saving && t == h->tokens + 5;
    int r = step(h, redirect ? 'D' : 'P');
    CHECK(t->refs && offset <= (MMU32)t->bytes && count <= (MMU32)t->bytes - offset);
    if (r <= 0)
        return 0;
    memcpy(out, t->data + offset, count);
    h->redirected |= redirect;
    return 1;
}
static int write_file(void *v, void *p, MMU32 offset, const void *data, size_t count)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    int r = step(h, h->profile == MM_TEXT_PLAIN && h->config.unit == 2 && offset == 0 ? 'B' : 'W');
    CHECK(t == h->tokens + 5 && t->refs && offset == (MMU32)t->bytes && count <= sizeof(t->data) - offset);
    if (r <= 0)
        return 0;
    memcpy(t->data + offset, data, count);
    t->bytes += (MMI32)count;
    return 1;
}
static int read_piece(void *v, const MMFilePiece *p, void *out)
{
    return read_file(v, p->file, p->offset, out, p->length);
}
static void *resize_storage(void *v, void *p, MMU32 old, MMU32 bytes, int priority)
{
    Host *h = (Host *)v;
    (void)old;
    (void)priority;
    return h->refuse_allocation ? NULL : realloc(p, bytes);
}
static void free_storage(void *v, void *p, MMU32 bytes)
{
    (void)v;
    (void)bytes;
    free(p);
}
static void init_stream(Host *h, Token *t)
{
    MMStorageAllocator allocator;
    MMPieceFiles files;
    allocator.context = h;
    allocator.resize = resize_storage;
    allocator.release = free_storage;
    memset(&files, 0, sizeof(files));
    files.context = h;
    files.retain = retain_file;
    files.release = release_file;
    files.read = read_piece;
    CHECK(!t->live && mm_piece_table_init(&t->pieces, &allocator, &files, 0) == MM_DATA_OK);
    t->live = 1;
}
static MMPieceTable *pieces(void *p)
{
    Token *t = (Token *)p;
    CHECK(t->live);
    return &t->pieces;
}
static void unexpected_handle(void *v, void *p)
{
    (void)v;
    (void)p;
    CHECK(0);
}
static int allocate_handle(void *v, MMU32 bytes, void **out)
{
    (void)bytes;
    unexpected_handle(v, out);
    return 0;
}
static int resize_handle(void *v, void **p, MMU32 bytes)
{
    return allocate_handle(v, bytes, p);
}
static MMU32 size_handle(void *v, void *p)
{
    unexpected_handle(v, p);
    return 0;
}
static void *lock_handle(void *v, void *p)
{
    unexpected_handle(v, p);
    return NULL;
}
static const MMHandleServices handles = {
    NULL, allocate_handle, unexpected_handle, size_handle, lock_handle, unexpected_handle, resize_handle, NULL
};
static MMI32 length(void *v, void *p)
{
    (void)v;
    return ((Token *)p)->bytes;
}
static int open_file(void *v, void *name, void **out)
{
    Host *h = (Host *)v;
    int r = step(h, 'O');
    CHECK(name == &h->requested);
    if (r > 0 || h->partial)
    {
        *out = h->tokens + 5;
        h->tokens[5].refs = 1;
    }
    return r;
}
static int new_stream(void *v, void **out)
{
    Host *h = (Host *)v;
    int r = step(h, 'S');
    if (r > 0 || h->partial)
    {
        *out = h->tokens + 6;
        h->tokens[6].refs = 1;
        init_stream(h, h->tokens + 6);
    }
    return r;
}
static int load(void *v, int system)
{
    Host *h = (Host *)v;
    int r = step(h, 'L');
    if (r > 0 && h->load_replacement)
    {
        release(v, MM_TEXT_OWNER_STREAM, h->slots[1]);
        h->tokens[11].refs = 1;
        init_stream(h, h->tokens + 11);
        CHECK(mm_piece_table_replace(&h->tokens[11].pieces, 0, 0, "xy", 2) == MM_DATA_OK);
        set(v, MM_TEXT_OWNER_STREAM, h->tokens + 11);
        return 1;
    }
    return r > 0 ? mm_text_base_load(system, &h->base) : r;
}
static int translate(void *v, MMTextLocation *p, int system)
{
    Host *h = (Host *)v;
    int r = step(h, 'T');
    CHECK(system == h->config.system && p->offset == 0);
    if ((r > 0 || h->partial) && h->change_file)
    {
        release(v, MM_TEXT_OWNER_FILE, p->file);
        p->file = h->tokens + 9;
        h->tokens[9].refs = 1;
        p->offset = 2;
    }
    if (r > 0 && h->translated_empty)
        p->bytes = 0;
    return r;
}
static void diagnostic(void *v, int kind)
{
    Host *h = (Host *)v;
    h->diagnostic = kind + 1;
    event(h, '!');
}
static void error(void *v, int kind)
{
    Host *h = (Host *)v;
    if (kind == MM_TEXT_DOC_READ)
        ++h->read_errors;
    else
        ++h->save_errors;
    event(h, 'E');
}
static int name(void *v, int profile, void *owner, void *out)
{
    Host *h = (Host *)v;
    int r = step(h, 'N');
    CHECK(profile == h->profile && owner == h->tokens + (profile == MM_TEXT_RICH ? 2 : 0));
    if (r > 0)
    {
        *(int *)out = 99;
        ++h->names;
    }
    return r;
}
static int is_temp(void *v, int profile, void *p)
{
    Host *h = (Host *)v;
    CHECK(profile == h->profile);
    return ((Token *)p)->temp;
}
static int create(void *v, int profile, void *name, void **out)
{
    Host *h = (Host *)v;
    int r = step(h, 'C'), index = profile == MM_TEXT_RICH ? 7 : 5;
    CHECK(profile == h->profile && (name == &h->requested || (name == &h->fallback && h->fallback == 99)));
    if (r > 0 || h->partial)
    {
        *out = h->tokens + index;
        h->tokens[index].refs = 1;
        h->tokens[index].temp = 1;
        h->tokens[index].bytes = 0;
    }
    return r;
}
static int chunk(void *v, void *archive, int adopt)
{
    Host *h = (Host *)v;
    CHECK(archive == h->tokens + 7);
    h->redirect_flag = adopt;
    return step(h, 'H');
}
static int save(void *v, void *archive)
{
    Host *h = (Host *)v;
    CHECK(archive == h->tokens + 7);
    return step(h, 'V');
}
static int rename_file(void *v, int profile, void *owner, void *name)
{
    Host *h = (Host *)v;
    CHECK(profile == h->profile && owner == h->tokens + (profile == MM_TEXT_RICH ? 7 : 5) &&
          (name == &h->requested || name == &h->fallback));
    return step(h, 'R');
}
static void keep(void *v, int profile, void *p)
{
    Host *h = (Host *)v;
    CHECK(profile == h->profile);
    ((Token *)p)->temp = 0;
    event(h, 'K');
}
static void clean(void *v)
{
    Host *h = (Host *)v;
    h->dirty = 0;
    event(h, 'Q');
}
static int open_archive(void *v, void *name, void **out)
{
    Host *h = (Host *)v;
    int r = step(h, 'O');
    CHECK(name == &h->requested);
    if (r > 0 || h->partial)
    {
        *out = h->tokens + 7;
        h->tokens[7].refs = 1;
    }
    return r;
}
static int first(void *v, void *archive, MMU32 type, MMTextChunk *out)
{
    Host *h = (Host *)v;
    CHECK(archive == h->tokens + 7 && type == MM_TEXT_ROOT);
    out->type = type;
    out->number = 42;
    return step(h, 'I');
}
static int read_chunk(void *v, void *archive, const MMTextChunk *p)
{
    Host *h = (Host *)v;
    int r = step(h, 'J');
    CHECK(archive == h->tokens + 7 && p->number == 42);
    if (r > 0 || h->partial)
    {
        h->slots[1] = h->tokens + 6;
        h->tokens[6].refs = 1;
        h->slots[3] = h->tokens + 8;
        h->tokens[8].refs = 1;
    }
    return r;
}
static int rich_base(void *v)
{
    Host *h = (Host *)v;
    int r = step(h, 'X');
    if (r > 0 || h->partial)
    {
        h->slots[1] = h->tokens + 6;
        h->tokens[6].refs = 1;
    }
    return r;
}
static int properties(void *v, MMU32 width, void **out)
{
    Host *h = (Host *)v;
    int r = step(h, 'G');
    CHECK(width == 8);
    if (r > 0 || h->partial)
    {
        *out = h->tokens + 8;
        h->tokens[8].refs = 1;
    }
    return r;
}
static void min_grow(void *v, void *p, MMU32 n)
{
    Host *h = (Host *)v;
    CHECK(p == h->tokens + 8 && n == 10);
    event(h, 'g');
}
static int font(void *v, int system, MMI32 height)
{
    Host *h = (Host *)v;
    CHECK(system == h->config.system && height == 12);
    return step(h, 'F');
}
static int object(void *v, void **out)
{
    Host *h = (Host *)v;
    int r = step(h, 'C');
    if (r > 0 || h->partial)
    {
        *out = h->tokens + 11;
        h->tokens[11].refs = 1;
    }
    return r;
}
static int init_object(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == h->tokens + 11);
    return step(h, 'I');
}
static void release_object(void *v, void *p)
{
    release(v, 0, p);
}
static void read_error(void *v)
{
    error(v, MM_TEXT_DOC_READ);
}
static void setup(Host *h, unsigned unit, int big)
{
    MMTextDocumentObjects o;
    unsigned i;
    memset(h, 0, sizeof(*h));
    h->config.unit = unit;
    h->config.big = big;
    h->config.system = 0x303;
    h->dirty = 1;
    for (i = 0; i < 12; ++i)
        h->tokens[i].bytes = 4;
    memcpy(h->tokens[5].data, "1234", 4);
    memcpy(h->tokens[9].data, "--5678", 6);
    h->tokens[9].bytes = 6;
    h->tokens[10].refs = 1;
    o.context = h;
    o.get = get;
    o.set = set;
    o.retain = retain;
    o.release = release;
    h->base.context = h;
    h->base.objects = o;
    h->base.pieces = pieces;
    h->base.file_bytes = length;
    h->base.open = open_file;
    h->base.new_stream = new_stream;
    h->base.load = load;
    h->base.translate = translate;
    h->base.diagnostic = diagnostic;
    h->rich.context = h;
    h->rich.objects = o;
    h->rich.open = open_archive;
    h->rich.first = first;
    h->rich.read = read_chunk;
    h->rich.base = rich_base;
    h->rich.properties = properties;
    h->rich.min_grow = min_grow;
    h->rich.font = font;
    h->rich.error = error;
    h->file.context = h;
    h->file.objects = o;
    h->file.name = name;
    h->file.is_temp = is_temp;
    h->block_files.context = h;
    h->block_files.retain = retain_file;
    h->block_files.release = release_file;
    h->block_files.create_temp = unexpected_temp;
    h->block_files.read = read_file;
    h->block_files.write = write_file;
    h->file.pieces = pieces;
    h->file.handles = &handles;
    h->file.files = &h->block_files;
    h->file.create = create;
    h->file.chunk = chunk;
    h->file.save = save;
    h->file.rename = rename_file;
    h->file.keep = keep;
    h->file.clean = clean;
    h->file.error = error;
    h->file.diagnostic = diagnostic;
    h->factory.context = h;
    h->factory.create = object;
    h->factory.init = init_object;
    h->factory.release = release_object;
    h->factory.read_error = read_error;
}
static void dispose(Host *h)
{
    unsigned i;
    mm_text_document_dispose(1, &h->base.objects);
    mm_text_document_dispose(0, &h->base.objects);
    for (i = 0; i < 12; ++i)
        CHECK(h->tokens[i].refs == (i == 10 ? 1 : 0));
    release(h, MM_TEXT_OWNER_STREAM, h->tokens + 10);
}
static void file_owners(Host *h, int profile)
{
    unsigned char initial[6] = {'A', 'B', 'C', 'D', 0, 0};
    h->profile = profile;
    h->slots[profile == MM_TEXT_RICH ? 2 : 0] = h->tokens + (profile == MM_TEXT_RICH ? 2 : 0);
    h->tokens[profile == MM_TEXT_RICH ? 2 : 0].refs = 1;
    h->slots[1] = h->tokens + 1;
    h->tokens[1].refs = 1;
    if (profile != MM_TEXT_RICH)
    {
        h->saving = 1;
        init_stream(h, h->tokens + 1);
        initial[h->config.unit == 2 && h->config.big ? 5 : 4] = 13;
        CHECK(mm_piece_table_replace(pieces(h->slots[1]), 0, 0, initial,
                                     profile == MM_TEXT_RAW ? 5 : 4 + h->config.unit) == MM_DATA_OK);
    }
}
static void saves(void)
{
    Host h;
    int profile, adopt, normal, big, partial, f, i, n, r, expected;
    const unsigned char *data;
    const MMTextDocumentConfig *config;
    unsigned unit;
    for (profile = MM_TEXT_PLAIN; profile <= MM_TEXT_RAW; ++profile)
        for (unit = 1; unit <= 2; ++unit)
            for (big = 0; big < 2; ++big)
                for (adopt = 0; adopt < 2; ++adopt)
                    for (normal = 0; normal < 2; ++normal)
                    {
                        setup(&h, unit, big);
                        file_owners(&h, profile);
                        config = profile == MM_TEXT_RAW ? NULL : &h.config;
                        CHECK(mm_text_document_save(profile, config, normal ? 0 : &h.requested, &h.fallback, adopt,
                                                    &h.file) == 1 &&
                              !h.save_errors);
                        CHECK(h.dirty == (!(adopt || normal)) && h.names == normal &&
                              (adopt || normal) == (h.slots[profile == MM_TEXT_RICH ? 2 : 0] ==
                                                    h.tokens + (profile == MM_TEXT_RICH ? 7 : 5)));
                        if (profile == MM_TEXT_RICH)
                            CHECK(h.redirect_flag == (adopt || normal));
                        else if (profile == MM_TEXT_RAW)
                            CHECK(h.tokens[5].bytes == 5 && !memcmp(h.tokens[5].data, "ABCD", 4) &&
                                  h.tokens[5].data[4] == (unit == 2 && big ? 0 : 13) && h.redirected == (adopt || normal));
                        else
                        {
                            data = h.tokens[5].data;
                            CHECK(h.tokens[5].bytes == (unit == 2 ? 6 : 4));
                            if (unit == 2)
                                CHECK(data[big ? 1 : 0] == 0xff && data[big ? 0 : 1] == 0xfe);
                            CHECK(!memcmp(data + (unit == 2 ? 2 : 0), "ABCD", 4));
                            CHECK(h.redirected == (adopt || normal));
                        }
                        n = h.calls;
                        dispose(&h);
                        for (partial = 0; partial < 2; ++partial)
                            for (f = -2; f <= 0; ++f)
                                for (i = 1; i <= n; ++i)
                                {
                                    setup(&h, unit, big);
                                    file_owners(&h, profile);
                                    h.partial = partial;
                                    h.fail_at = i;
                                    h.failure = f;
                                    r = mm_text_document_save(profile, config, normal ? 0 : &h.requested,
                                                              &h.fallback, adopt, &h.file);
                                    if (h.failed == 'D')
                                        CHECK(r == 1 && !h.dirty && !h.save_errors);
                                    else
                                    {
                                        expected = h.failed == 'B' || h.failed == 'W' ? 0 : f;
                                        CHECK(r == expected && h.dirty &&
                                              h.slots[profile == MM_TEXT_RICH ? 2 : 0] ==
                                                  h.tokens + (profile == MM_TEXT_RICH ? 2 : 0));
                                        CHECK(h.save_errors == (!f && profile == MM_TEXT_RICH && h.failed != 'N'));
                                        if (h.save_errors && (h.partial || h.failed != 'C'))
                                            CHECK(strchr(h.log, 'x') < strchr(h.log, 'E'));
                                        if (h.failed == 'R' && profile != MM_TEXT_RICH && (adopt || normal))
                                            CHECK(h.redirected && !h.tokens[5].refs && h.tokens[5].temp &&
                                                  strchr(h.log, 'D') < strchr(h.log, 'R'));
                                    }
                                    dispose(&h);
                                }
                    }
    setup(&h, 1, 0);
    CHECK(!mm_text_document_save(MM_TEXT_RICH, &h.config, 0, &h.fallback, 0, &h.file) &&
          h.diagnostic == MM_TEXT_DOC_NO_FILE + 1 && !h.calls && !h.save_errors);
    for (profile = MM_TEXT_PLAIN; profile <= MM_TEXT_RAW; ++profile)
    {
        setup(&h, 1, 0);
        file_owners(&h, profile);
        h.tokens[profile == MM_TEXT_RICH ? 2 : 0].temp = 1;
        h.returned = 22;
        CHECK(mm_text_document_name(profile, &h.returned, &h.file) == (profile == MM_TEXT_RICH) &&
              h.returned == (profile == MM_TEXT_RICH ? 99 : 22));
        CHECK(mm_text_document_save(profile, profile == MM_TEXT_RAW ? NULL : &h.config, 0, &h.fallback, 0, &h.file) == 1 &&
              !h.dirty);
        dispose(&h);
    }
}
static Token *borrowed(Host *h, MMU32 length)
{
    Token *t = h->tokens + 10;
    init_stream(h, t);
    CHECK(mm_piece_table_replace(&t->pieces, 0, 0, "ABCD", length) == MM_DATA_OK);
    return t;
}
static void base_init(void)
{
    Host h;
    int named, borrow, big, partial, f, i, n, r, change;
    unsigned unit, bytes;
    unsigned char content[6];
    MMPieceTable *stream;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
            for (named = 0; named < 2; ++named)
                for (borrow = 0; borrow < 2; ++borrow)
                    for (change = 0; change < 2; ++change)
                    {
                        setup(&h, unit, big);
                        h.change_file = change;
                        CHECK(mm_text_base_init(MM_TEXT_PLAIN, &h.config, named ? &h.requested : 0, borrow ? borrowed(&h, 4) : 0,
                                                h.config.system, &h.base) == 1);
                        stream = pieces(h.slots[1]);
                        bytes = named || borrow ? 4 : 0;
                        CHECK(stream->length == bytes + unit);
                        CHECK(mm_piece_table_read(stream, 0, content, bytes + unit) == MM_DATA_OK);
                        CHECK(content[bytes + (unit == 2 && big ? 1 : 0)] == 13 &&
                              (unit == 1 || !content[bytes + (big ? 0 : 1)]));
                        if (bytes)
                            CHECK(!memcmp(content, borrow ? "ABCD" : change ? "5678" : "1234", bytes));
                        n = h.calls;
                        dispose(&h);
                        for (partial = 0; partial < 2; ++partial)
                            for (f = -2; f <= 0; ++f)
                                for (i = 1; i <= n; ++i)
                                {
                                    setup(&h, unit, big);
                                    h.change_file = change;
                                    h.partial = partial;
                                    h.fail_at = i;
                                    h.failure = f;
                                    r = mm_text_base_init(MM_TEXT_PLAIN, &h.config, named ? &h.requested : 0,
                                                          borrow ? borrowed(&h, 4) : 0, h.config.system, &h.base);
                                    CHECK(r == (h.failed == 'P' ? 0 : f));
                                    dispose(&h);
                                }
                    }
    setup(&h, 2, 0);
    CHECK(!mm_text_base_init(MM_TEXT_PLAIN, &h.config, &h.requested, h.tokens + 10, 0x202, &h.base) &&
          h.diagnostic == MM_TEXT_DOC_ENCODING + 1 && h.slots[0] && !h.slots[1]);
    dispose(&h);
    setup(&h, 2, 0);
    CHECK(!mm_text_base_init(MM_TEXT_PLAIN, &h.config, 0, borrowed(&h, 3), h.config.system, &h.base) &&
          h.diagnostic == MM_TEXT_DOC_PARTIAL_CHAR + 1 && h.tokens[10].refs == 1 && !h.slots[1]);
    dispose(&h);
    setup(&h, 1, 0);
    h.translated_empty = 1;
    CHECK(mm_text_base_init(MM_TEXT_PLAIN, &h.config, &h.requested, 0, h.config.system, &h.base) == 1 && !strchr(h.log, 'P'));
    dispose(&h);
    setup(&h, 1, 0);
    h.tokens[5].bytes = 0;
    CHECK(mm_text_base_init(MM_TEXT_PLAIN, &h.config, &h.requested, 0, h.config.system, &h.base) == 1 && !strchr(h.log, 'T'));
    dispose(&h);
    setup(&h, 1, 0);
    h.load_replacement = 1;
    CHECK(mm_text_base_init(MM_TEXT_PLAIN, &h.config, 0, 0, h.config.system, &h.base) == 1 && h.slots[1] == h.tokens + 11 &&
          pieces(h.slots[1])->length == 3 && !h.tokens[6].refs && !h.tokens[6].live);
    CHECK(mm_piece_table_read(pieces(h.slots[1]), 0, content, 3) == MM_DATA_OK && !memcmp(content, "xy\r", 3));
    dispose(&h);
    setup(&h, 1, 0);
    h.refuse_allocation = 1;
    CHECK(!mm_text_base_init(MM_TEXT_PLAIN, &h.config, 0, 0, h.config.system, &h.base) && h.slots[1] && !pieces(h.slots[1])->length);
    dispose(&h);
}
static void raw_init(void)
{
    Host h;
    unsigned char content[5];
    static const unsigned char raw[] = {0xef, 0xbb, 0xbf, 0, 13};
    MMPieceTable *stream;
    setup(&h, 2, 1);
    h.tokens[5].bytes = sizeof(raw);
    memcpy(h.tokens[5].data, raw, sizeof(raw));
    CHECK(mm_text_base_init(MM_TEXT_RAW, NULL, &h.requested, NULL, 0, &h.base) == 1);
    stream = pieces(h.slots[1]);
    CHECK(stream->length == sizeof(raw) && mm_piece_table_read(stream, 0, content, sizeof(content)) == MM_DATA_OK);
    CHECK(!memcmp(raw, content, sizeof(content)) && !strchr(h.log, 'L') && !strchr(h.log, 'T'));
    dispose(&h);
    setup(&h, 2, 1);
    CHECK(mm_text_base_init(MM_TEXT_RAW, NULL, &h.requested, borrowed(&h, 3), 0, &h.base) == 1);
    CHECK(h.slots[1] == h.tokens + 10 && h.tokens[10].refs == 2 && pieces(h.slots[1])->length == 3);
    CHECK(!strchr(h.log, 'P') && !strchr(h.log, 'T') && !strchr(h.log, 'S'));
    dispose(&h);
}
static void file_backed_text(void)
{
    Host h;
    MMPieceTable *stream;
    MMFilePiece source, saved;
    unsigned char tail[4];
    int fail;
    setup(&h, 1, 0);
    h.change_file = 1;
    h.tokens[5].bytes = 2048;
    h.tokens[9].bytes = 2050;
    memset(h.tokens[9].data + 2, 'T', 2048);
    CHECK(mm_text_base_init(MM_TEXT_PLAIN, &h.config, &h.requested, NULL, h.config.system, &h.base) == 1);
    stream = pieces(h.slots[1]);
    CHECK(mm_piece_table_get(stream, 0, &saved, NULL) == MM_DATA_OK);
    CHECK(saved.file == h.tokens + 9 && saved.offset == 2 && saved.length == 2048 && h.tokens[9].refs == 1);
    CHECK(mm_piece_table_read(stream, 2046, tail, 3) == MM_DATA_OK && !memcmp(tail, "TT\r", 3));
    dispose(&h);

    for (fail = 0; fail <= 1; ++fail)
    {
        setup(&h, 2, 0);
        file_owners(&h, MM_TEXT_PLAIN);
        stream = pieces(h.slots[1]);
        source.file = h.tokens;
        source.offset = 0;
        source.length = 2050;
        h.tokens[0].bytes = 2050;
        memset(h.tokens[0].data, 'P', 2050);
        CHECK(mm_piece_table_replace_file(stream, 0, 4, &source, 0) == MM_DATA_OK);
        if (fail)
            h.fail_at = 5;
        CHECK(mm_text_document_save(MM_TEXT_PLAIN, &h.config, &h.requested, &h.fallback, 1, &h.file) == !fail);
        CHECK(h.dirty == fail && h.tokens[0].refs == fail && h.tokens[5].refs == 2 - fail);
        CHECK(h.tokens[5].temp == fail && (h.failed == 'R') == fail);
        CHECK(h.tokens[5].bytes == 2052 && h.tokens[5].data[0] == 0xff && h.tokens[5].data[1] == 0xfe);
        CHECK(!memcmp(h.tokens[5].data + 2, h.tokens[0].data, 2050));
        CHECK(mm_piece_table_get(stream, 0, &saved, NULL) == MM_DATA_OK);
        CHECK(saved.file == h.tokens + 5 && saved.offset == 2 && saved.length == 2050);
        CHECK(mm_piece_table_read(stream, 2048, tail, 4) == MM_DATA_OK && !memcmp(tail, "PP\r\0", 4));
        dispose(&h);
    }
}
static void rich_init(void)
{
    Host h;
    int named, partial, f, i, n, r;
    for (named = 0; named < 2; ++named)
    {
        setup(&h, 1, 0);
        CHECK(mm_text_rich_init(&h.config, named ? &h.requested : 0, MM_TEXT_ROOT, &h.rich) == 1);
        CHECK(h.slots[1] && h.slots[3] && (!!h.slots[2]) == named);
        n = h.calls;
        dispose(&h);
        for (partial = 0; partial < 2; ++partial)
            for (f = -2; f <= 0; ++f)
                for (i = 1; i <= n; ++i)
                {
                    setup(&h, 1, 0);
                    h.partial = partial;
                    h.fail_at = i;
                    h.failure = f;
                    r = mm_text_rich_init(&h.config, named ? &h.requested : 0, MM_TEXT_ROOT, &h.rich);
                    CHECK(r == f && !h.slots[2]);
                    CHECK(h.read_errors == (named && !f));
                    if (h.read_errors && (h.partial || i > 1))
                        CHECK(strchr(h.log, 'x') < strchr(h.log, 'E'));
                    dispose(&h);
                }
    }
}
static void factories(void)
{
    Host h;
    void *out;
    int flag, partial, f, i;
    for (flag = 0; flag < 2; ++flag)
    {
        setup(&h, 1, 0);
        CHECK(mm_text_document_create(&out, flag, &h.factory) == 1 && out == h.tokens + 11 && !h.read_errors);
        release_object(&h, out);
        for (partial = 0; partial < 2; ++partial)
            for (f = -2; f <= 0; ++f)
                for (i = 1; i <= 2; ++i)
                {
                    setup(&h, 1, 0);
                    h.partial = partial;
                    h.fail_at = i;
                    h.failure = f;
                    out = &h;
                    CHECK(mm_text_document_create(&out, flag, &h.factory) == f && !out && !h.tokens[11].refs &&
                          h.read_errors == (flag && i == 2 && !f));
                    if (h.read_errors)
                        CHECK(strchr(h.log, 'E') < strchr(h.log, 'x'));
                }
    }
    setup(&h, 1, 0);
    {
        int i;
        for (i = 0; i < 5; ++i)
        {
            h.slots[i] = h.tokens + i;
            h.tokens[i].refs = 1;
        }
    }
    mm_text_document_dispose(1, &h.base.objects);
    CHECK(!strcmp(h.log, "x3x4x2"));
    mm_text_document_dispose(0, &h.base.objects);
    CHECK(!strcmp(h.log, "x3x4x2x1x0"));
    dispose(&h);
}
int main(void)
{
    saves();
    base_init();
    raw_init();
    file_backed_text();
    rich_init();
    factories();
    puts("Text documents: file transactions, BOM/sentinel rules, partial owners, translated views, factories and "
         "exhaustive failures passed.");
    return 0;
}
