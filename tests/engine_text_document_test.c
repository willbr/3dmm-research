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
    int refs, temp;
    MMI32 bytes;
    struct Token *backing;
} Token;
typedef struct Host
{
    MMTextBaseServices base;
    MMTextRichInitServices rich;
    MMTextFileServices file;
    MMTextFactoryServices factory;
    MMTextDocumentConfig config;
    Token tokens[12];
    void *slots[5];
    int calls, fail_at, failure, partial, change_file, translated_empty, profile, dirty, redirected, redirect_flag,
        read_errors, save_errors, diagnostic;
    int names, requested, fallback, returned, load_replacement;
    unsigned char bom[2], cr[2];
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
    (void)kind;
    CHECK(t->refs > 0);
    --t->refs;
    event(h, 'x');
    if (!t->refs && t->backing)
    {
        release(v, MM_TEXT_OWNER_FILE, t->backing);
        t->backing = 0;
    }
}
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
        h->tokens[6].bytes = 0;
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
        h->tokens[11].bytes = 2;
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
static void backing(Host *h, Token *stream, Token *file)
{
    if (stream->backing)
        release(h, MM_TEXT_OWNER_FILE, stream->backing);
    stream->backing = file;
    retain(h, MM_TEXT_OWNER_FILE, file);
}
static int replace(void *v, void *stream, const MMTextLocation *p, MMI32 bytes)
{
    Host *h = (Host *)v;
    Token *s = (Token *)stream;
    int r = step(h, 'P');
    CHECK(bytes == s->bytes);
    if (r > 0)
    {
        backing(h, s, (Token *)p->file);
        s->bytes = p->bytes;
    }
    return r;
}
static int append(void *v, void *stream, const void *p, MMU32 bytes)
{
    Host *h = (Host *)v;
    int r = step(h, 'A');
    CHECK(bytes == h->config.unit);
    memcpy(h->cr, p, bytes);
    if (r > 0)
        ((Token *)stream)->bytes += (MMI32)bytes;
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
    }
    return r;
}
static int put(void *v, const MMTextLocation *p, const void *bytes, MMU32 count)
{
    Host *h = (Host *)v;
    CHECK(p->file == h->tokens + 5 && p->offset == 0 && p->bytes == 6 && count == 2);
    memcpy(h->bom, bytes, 2);
    return step(h, 'B');
}
static int write_stream(void *v, void *stream, const MMTextLocation *p)
{
    Host *h = (Host *)v;
    CHECK(stream == h->tokens + 1 && p->file == h->tokens + 5 && p->bytes == 4 &&
          p->offset == (h->config.unit == 2 ? 2U : 0U));
    return step(h, 'W');
}
static int redirect(void *v, void *stream, const MMTextLocation *p)
{
    Host *h = (Host *)v;
    int r = step(h, 'D');
    CHECK(stream == h->tokens + 1 && p->file == h->tokens + 5 && p->bytes == 4);
    if (r > 0)
    {
        backing(h, (Token *)stream, (Token *)p->file);
        h->redirected = 1;
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
    h->tokens[1].bytes = 4 + (MMI32)unit;
    h->tokens[10].refs = 1;
    o.context = h;
    o.get = get;
    o.set = set;
    o.retain = retain;
    o.release = release;
    h->base.context = h;
    h->base.objects = o;
    h->base.stream_bytes = length;
    h->base.file_bytes = length;
    h->base.open = open_file;
    h->base.new_stream = new_stream;
    h->base.load = load;
    h->base.translate = translate;
    h->base.replace = replace;
    h->base.append = append;
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
    h->file.stream_bytes = length;
    h->file.create = create;
    h->file.put = put;
    h->file.write = write_stream;
    h->file.redirect = redirect;
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
}
static void file_owners(Host *h, int profile)
{
    h->profile = profile;
    h->slots[profile == MM_TEXT_RICH ? 2 : 0] = h->tokens + (profile == MM_TEXT_RICH ? 2 : 0);
    h->tokens[profile == MM_TEXT_RICH ? 2 : 0].refs = 1;
    h->slots[1] = h->tokens + 1;
    h->tokens[1].refs = 1;
}
static void saves(void)
{
    Host h;
    int profile, adopt, normal, big, partial, f, i, n, r;
    unsigned unit;
    for (profile = 0; profile < 2; ++profile)
        for (unit = 1; unit <= 2; ++unit)
            for (big = 0; big < 2; ++big)
                for (adopt = 0; adopt < 2; ++adopt)
                    for (normal = 0; normal < 2; ++normal)
                    {
                        setup(&h, unit, big);
                        file_owners(&h, profile);
                        CHECK(mm_text_document_save(profile, &h.config, normal ? 0 : &h.requested, &h.fallback, adopt,
                                                    &h.file) == 1 &&
                              !h.save_errors);
                        CHECK(h.dirty == (!(adopt || normal)) && h.names == normal &&
                              (adopt || normal) == (h.slots[profile == MM_TEXT_RICH ? 2 : 0] ==
                                                    h.tokens + (profile == MM_TEXT_RICH ? 7 : 5)));
                        if (profile == MM_TEXT_RICH)
                            CHECK(h.redirect_flag == (adopt || normal));
                        if (profile == MM_TEXT_PLAIN && unit == 2)
                            CHECK(h.bom[big ? 1 : 0] == 0xff && h.bom[big ? 0 : 1] == 0xfe);
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
                                    r = mm_text_document_save(profile, &h.config, normal ? 0 : &h.requested,
                                                              &h.fallback, adopt, &h.file);
                                    if (!f && h.failed == 'D')
                                        CHECK(r == 1 && !h.dirty && !h.save_errors);
                                    else
                                    {
                                        CHECK(r == f && h.dirty &&
                                              h.slots[profile == MM_TEXT_RICH ? 2 : 0] ==
                                                  h.tokens + (profile == MM_TEXT_RICH ? 2 : 0));
                                        CHECK(h.save_errors == (!f && profile == MM_TEXT_RICH && h.failed != 'N'));
                                        if (h.save_errors && (h.partial || h.failed != 'C'))
                                            CHECK(strchr(h.log, 'x') < strchr(h.log, 'E'));
                                        if (h.failed == 'R' && profile == MM_TEXT_PLAIN && (adopt || normal))
                                            CHECK(h.redirected && h.tokens[5].refs == 1 && h.tokens[5].temp);
                                    }
                                    dispose(&h);
                                }
                    }
    setup(&h, 1, 0);
    CHECK(!mm_text_document_save(MM_TEXT_RICH, &h.config, 0, &h.fallback, 0, &h.file) &&
          h.diagnostic == MM_TEXT_DOC_NO_FILE + 1 && !h.calls && !h.save_errors);
    for (profile = 0; profile < 2; ++profile)
    {
        setup(&h, 1, 0);
        file_owners(&h, profile);
        h.tokens[profile == MM_TEXT_RICH ? 2 : 0].temp = 1;
        h.returned = 22;
        CHECK(mm_text_document_name(profile, &h.returned, &h.file) == (profile == MM_TEXT_RICH) &&
              h.returned == (profile == MM_TEXT_RICH ? 99 : 22));
        CHECK(mm_text_document_save(profile, &h.config, 0, &h.fallback, 0, &h.file) == 1 && !h.dirty);
        dispose(&h);
    }
}
static void base_init(void)
{
    Host h;
    int named, borrow, big, partial, f, i, n, r, change;
    unsigned unit;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
            for (named = 0; named < 2; ++named)
                for (borrow = 0; borrow < 2; ++borrow)
                    for (change = 0; change < 2; ++change)
                    {
                        setup(&h, unit, big);
                        h.change_file = change;
                        CHECK(mm_text_base_init(&h.config, named ? &h.requested : 0, borrow ? h.tokens + 10 : 0,
                                                h.config.system, &h.base) == 1);
                        CHECK(h.cr[unit == 2 && big ? 1 : 0] == 13 && (unit == 1 || !h.cr[big ? 0 : 1]));
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
                                    r = mm_text_base_init(&h.config, named ? &h.requested : 0,
                                                          borrow ? h.tokens + 10 : 0, h.config.system, &h.base);
                                    CHECK(r == f);
                                    dispose(&h);
                                }
                    }
    setup(&h, 2, 0);
    CHECK(!mm_text_base_init(&h.config, &h.requested, h.tokens + 10, 0x202, &h.base) &&
          h.diagnostic == MM_TEXT_DOC_ENCODING + 1 && h.slots[0] && !h.slots[1]);
    dispose(&h);
    setup(&h, 2, 0);
    h.tokens[10].bytes = 3;
    CHECK(!mm_text_base_init(&h.config, 0, h.tokens + 10, h.config.system, &h.base) &&
          h.diagnostic == MM_TEXT_DOC_PARTIAL_CHAR + 1 && h.tokens[10].refs == 1 && !h.slots[1]);
    setup(&h, 1, 0);
    h.translated_empty = 1;
    CHECK(mm_text_base_init(&h.config, &h.requested, 0, h.config.system, &h.base) == 1 && !strchr(h.log, 'P'));
    dispose(&h);
    setup(&h, 1, 0);
    h.tokens[5].bytes = 0;
    CHECK(mm_text_base_init(&h.config, &h.requested, 0, h.config.system, &h.base) == 1 && !strchr(h.log, 'T'));
    dispose(&h);
    setup(&h, 1, 0);
    h.load_replacement = 1;
    CHECK(mm_text_base_init(&h.config, 0, 0, h.config.system, &h.base) == 1 && h.slots[1] == h.tokens + 11 &&
          h.tokens[11].bytes == 3 && !h.tokens[6].refs && !h.tokens[6].bytes);
    dispose(&h);
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
    rich_init();
    factories();
    puts("Text documents: file transactions, BOM/sentinel rules, partial owners, translated views, factories and "
         "exhaustive failures passed.");
    return 0;
}
