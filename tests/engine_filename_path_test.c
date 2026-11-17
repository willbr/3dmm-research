#include "mm/filename_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{ if (!value) { fprintf(stderr, "Filename path check failed at %d\n", line); exit(1); } }
#define CHECK(x) check(!!(x), __LINE__)
typedef union Buffer { unsigned char bytes[257]; unsigned short words[257]; } Buffer;
typedef struct Name { MMFilenameState state; Buffer buffer; MMFilenameValue value; } Name;
typedef struct Fixture {
    MMFilenamePathHost host;
    Name names[4];
    unsigned unit, validations, copies, full_calls, find_calls, info_calls, remove_calls, rename_calls, mkdir_calls, open_calls;
    unsigned copy_names, issue_count[7], assertion_count;
    int last_flags, last_assertion, last_issue, issue_owner, search_null;
    MMI32 issue_type, issue_length, reported_length;
    int full_fail, find_fail, status, directory, hidden, system, remove_ok, rename_ok, mkdir_ok, opened;
    char reply[300], input[300], search[300], a[300], b[300];
} Fixture;
static unsigned get(const void *p, unsigned size, int i)
{ return size == 1 ? ((const unsigned char *)p)[i] : ((const unsigned short *)p)[i]; }
static void put(void *p, unsigned size, int i, unsigned ch)
{ if (size == 1) ((unsigned char *)p)[i] = (unsigned char)ch; else ((unsigned short *)p)[i] = (unsigned short)ch; }
static int length(const MMStringValue *s) { return (int)get(s->buffer, s->unit, 0); }
static void capture(Fixture *f, const void *p, char *out)
{
    int i = 0; unsigned ch;
    while ((ch = get(p, f->unit, i)) != 0) { CHECK(i < 256); out[i++] = ch < 128 ? (char)ch : '?'; }
    out[i] = 0;
}
static void copy_check(void *c, const void *a, void *b, size_t bytes)
{ Fixture *f = (Fixture *)c; CHECK(a && b && bytes > 0); ++f->copies; }
static MMStringValue string(Fixture *f, Buffer *out, const char *text)
{
    MMStringValue s; int i, n = (int)strlen(text); CHECK(n <= 255);
    put(out, f->unit, 0, (unsigned)n);
    for (i = 0; i <= n; ++i) put(out, f->unit, i + 1, (unsigned char)text[i]);
    s.buffer = out; s.unit = f->unit; s.context = f; s.check_copy = copy_check; return s;
}
static void set(Fixture *f, Name *n, const char *text, MMI32 type)
{
    Buffer b; MMStringValue source = string(f, &b, text);
    n->state.type = type; mm_string_value_copy(&n->value.path, source.buffer);
}
static void equals(Name *n, const char *text)
{
    int i, count = (int)strlen(text); CHECK(length(&n->value.path) == count);
    for (i = 0; i <= count; ++i) CHECK(get(n->buffer.bytes, n->value.path.unit, i + 1) == (unsigned char)text[i]);
}
static unsigned upper(void *c, unsigned ch) { (void)c; return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch; }
static int is_separator(void *c, unsigned ch) { (void)c; return ch == '/' || ch == '\\'; }
static unsigned separator(void *c) { (void)c; return '/'; }
static void validate(void *c, const MMFilenameValue *v, int flags)
{ Fixture *f = (Fixture *)c; Name *n = (Name *)v->owner; CHECK(v->state == &n->state); ++f->validations; f->last_flags = flags; }
static void assertion(void *c, int code) { Fixture *f = (Fixture *)c; ++f->assertion_count; f->last_assertion = code; }
static void issue(void *c, const MMFilenameValue *v, int code)
{
    Fixture *f = (Fixture *)c; int i;
    CHECK(code >= 1 && code <= 6); ++f->issue_count[code]; f->last_issue = code;
    f->issue_type = v->state->type; f->issue_length = length(&v->path); f->issue_owner = -1;
    for (i = 0; i < 4; ++i) if (v->owner == &f->names[i]) f->issue_owner = i;
}
static MMI32 response(Fixture *f, void *out, MMI32 capacity)
{
    int i, count = (int)strlen(f->reply); CHECK(capacity == 255);
    if (f->reported_length) return f->reported_length;
    if (count >= capacity) return 0;
    for (i = 0; i <= count; ++i) put(out, f->unit, i, (unsigned char)f->reply[i]);
    return count;
}
static MMI32 full(void *c, const void *input, void *out, MMI32 capacity)
{ Fixture *f = (Fixture *)c; ++f->full_calls; capture(f, input, f->input); return f->full_fail ? 0 : response(f, out, capacity); }
static MMI32 find(void *c, const void *input, const void *search, void *out, MMI32 capacity)
{
    Fixture *f = (Fixture *)c; ++f->find_calls; capture(f, input, f->input); f->search_null = search == NULL;
    if (search) capture(f, search, f->search);
    return f->find_fail ? 0 : response(f, out, capacity);
}
static int info(void *c, const void *p, MMFilenameInfo *out)
{
    Fixture *f = (Fixture *)c; ++f->info_calls; capture(f, p, f->input);
    out->directory = f->directory; out->hidden = f->hidden; out->system = f->system; return f->status;
}
static int remove_file(void *c, const void *p)
{ Fixture *f = (Fixture *)c; ++f->remove_calls; capture(f, p, f->input); return f->remove_ok; }
static int rename_file(void *c, const void *a, const void *b)
{ Fixture *f = (Fixture *)c; ++f->rename_calls; capture(f, a, f->a); capture(f, b, f->b); return f->rename_ok; }
static int mkdir_file(void *c, const void *p)
{ Fixture *f = (Fixture *)c; ++f->mkdir_calls; capture(f, p, f->input); return f->mkdir_ok; }
static int equal(void *c, const void *a, const void *b)
{ Fixture *f = (Fixture *)c; capture(f, a, f->a); capture(f, b, f->b); return !strcmp(f->a, f->b); }
static MMU32 volume(void *c, const void *p) { Fixture *f = (Fixture *)c; capture(f, p, f->input); return 9; }
static int opened(void *c, const MMFilenameValue *v)
{ Fixture *f = (Fixture *)c; (void)v; ++f->open_calls; return f->opened; }
static void copy_name(void *c, const MMFilenameValue *dst, const MMFilenameValue *src)
{
    Fixture *f = (Fixture *)c; ++f->copy_names; dst->state->type = src->state->type;
    mm_string_value_copy(&dst->path, src->path.buffer);
}
static void init(Fixture *f, unsigned size)
{
    int i; MMFilenamePathHost *h;
    memset(f, 0, sizeof(*f)); f->unit = size; h = &f->host; h->context = f; h->value.context = f;
    h->value.upper = upper; h->value.separator = is_separator; h->value.validate = validate;
    h->full_path = full; h->find_path = find; h->separator = separator; h->info = info;
    h->remove = remove_file; h->rename = rename_file; h->make_directory = mkdir_file;
    h->equal = equal; h->volume_kinds = volume; h->is_open = opened; h->copy_name = copy_name;
    h->issue = issue; h->assertion = assertion; f->remove_ok = f->rename_ok = f->mkdir_ok = 1;
    memcpy(f->reply, "/p/file", sizeof("/p/file"));
    for (i = 0; i < 4; ++i) {
        Name *n = &f->names[i]; n->value.state = &n->state; n->value.owner = n;
        n->value.path.buffer = &n->buffer; n->value.path.unit = size;
        n->value.path.context = f; n->value.path.check_copy = copy_check;
        mm_filename_value_clear(&n->value, &h->value);
    }
}
int main(void)
{
    Fixture f; Buffer input_buffer, search_buffer, output_buffer;
    MMStringValue input, search, output;
    Name *n, *other, *a, *b;
    unsigned size, before;
    int i; char long_path[256];
    const char *dot_dirs[] = {"", ".", "..", "p/...", "p/", "p\\"};
    for (size = 1; size <= 2; ++size) {
        init(&f, size); n = &f.names[0]; other = &f.names[1]; a = &f.names[2]; b = &f.names[3];
        input = string(&f, &input_buffer, "file");
        CHECK(mm_filename_build(&n->value, &f.host, &input, 0x545854L)); equals(n, "/p/file.TXT"); CHECK(n->state.type == 0x545854L);
        input = string(&f, &input_buffer, "file.");
        CHECK(mm_filename_build(&n->value, &f.host, &input, 0x545854L)); equals(n, "/p/file"); CHECK(!n->state.type);
        for (i = 0; i < 6; ++i) {
            input = string(&f, &input_buffer, dot_dirs[i]);
            CHECK(mm_filename_build(&n->value, &f.host, &input, MM_FILENAME_NIL));
            equals(n, "/p/file/"); CHECK(n->state.type == MM_FILENAME_DIRECTORY);
        }
        f.full_fail = 1; input = string(&f, &input_buffer, "file");
        CHECK(!mm_filename_build(&n->value, &f.host, &input, MM_FILENAME_NIL));
        CHECK(f.issue_type == MM_FILENAME_NIL && f.issue_length == 0 && f.last_flags == MM_FILENAME_EMPTY);
        f.full_fail = 0; f.reported_length = 256;
        CHECK(!mm_filename_build(&n->value, &f.host, &input, MM_FILENAME_NIL)); CHECK(!f.issue_length);
        f.reported_length = 0; memset(f.reply, 'q', 251); f.reply[0] = '/'; f.reply[251] = 0;
        CHECK(!mm_filename_build(&n->value, &f.host, &input, 0x41424344L)); CHECK(!f.issue_length);
        memcpy(f.reply, "/found/a.bin", sizeof("/found/a.bin")); search = string(&f, &search_buffer, "/search");
        CHECK(mm_filename_search(&n->value, &f.host, &input, &search)); equals(n, "/found/a.bin");
        CHECK(!f.search_null && !strcmp(f.search, "/search"));
        search = string(&f, &search_buffer, "");
        CHECK(mm_filename_search(&n->value, &f.host, &input, &search) && !f.search_null);
        CHECK(mm_filename_search(&n->value, &f.host, &input, NULL) && f.search_null);
        f.find_fail = 1; CHECK(!mm_filename_search(&n->value, &f.host, &input, NULL));
        CHECK(f.issue_type == MM_FILENAME_NIL && !f.issue_length); f.find_fail = 0;
        set(&f, n, "/p/a.bin", 0x42494eL); f.status = MM_FILENAME_FOUND; f.directory = 0;
        CHECK(mm_filename_exists(&n->value, &f.host) == MM_FILENAME_YES);
        f.directory = f.hidden = 1; CHECK(mm_filename_exists(&n->value, &f.host) == MM_FILENAME_MAYBE && f.last_issue == MM_FILENAME_MISMATCH);
        f.directory = 0; CHECK(mm_filename_exists(&n->value, &f.host) == MM_FILENAME_MAYBE && f.last_issue == MM_FILENAME_HIDDEN);
        f.hidden = 0; f.system = 1; CHECK(mm_filename_exists(&n->value, &f.host) == MM_FILENAME_MAYBE && f.last_issue == MM_FILENAME_HIDDEN);
        before = f.issue_count[MM_FILENAME_GENERAL]; f.status = MM_FILENAME_MISSING;
        CHECK(mm_filename_exists(&n->value, &f.host) == MM_FILENAME_NO && f.issue_count[MM_FILENAME_GENERAL] == before);
        f.status = MM_FILENAME_INFO_ERROR; CHECK(mm_filename_exists(&n->value, &f.host) == MM_FILENAME_MAYBE && f.issue_count[MM_FILENAME_GENERAL] == before + 1);
        f.status = MM_FILENAME_FOUND; f.system = 0; f.directory = 1;
        set(&f, n, "/p/", MM_FILENAME_DIRECTORY); CHECK(mm_filename_exists(&n->value, &f.host) == MM_FILENAME_YES && !strcmp(f.input, "/p"));
        set(&f, n, "/", MM_FILENAME_DIRECTORY); CHECK(mm_filename_exists(&n->value, &f.host) == MM_FILENAME_YES && !strcmp(f.input, "/"));
        set(&f, n, "C:\\", MM_FILENAME_DIRECTORY); CHECK(mm_filename_exists(&n->value, &f.host) == MM_FILENAME_YES && !strcmp(f.input, "C:"));
        set(&f, n, "/p/a", 0); set(&f, other, "/p/b", 0);
        CHECK(!mm_filename_equal(&n->value, &f.host, &other->value) && !strcmp(f.a, "/p/b") && !strcmp(f.b, "/p/a"));
        CHECK(mm_filename_same_directory(&n->value, &f.host, &other->value, &a->value, &b->value)); equals(n, "/p/a"); equals(other, "/p/b");
        f.host.copy_name = NULL; CHECK(mm_filename_same_directory(&n->value, &f.host, &other->value, &a->value, &b->value));
        f.host.copy_name = copy_name; f.opened = 1; f.remove_ok = 0;
        CHECK(!mm_filename_delete(&n->value, &f.host) && f.last_assertion == MM_FILENAME_PATH_OPEN && f.last_issue == MM_FILENAME_DELETE);
        before = f.open_calls; f.host.assertion = NULL; f.remove_ok = 1;
        CHECK(mm_filename_delete(&n->value, &f.host) && f.open_calls == before); f.host.assertion = assertion;
        f.rename_ok = 0; CHECK(!mm_filename_rename(&n->value, &f.host, &other->value) && f.last_issue == MM_FILENAME_RENAME);
        f.rename_ok = 1; CHECK(mm_filename_rename(&n->value, &f.host, &other->value)); equals(n, "/p/a"); equals(other, "/p/b");
        CHECK(mm_filename_volume(&n->value, &f.host) == 9 && !strcmp(f.input, "/p/a"));
        set(&f, n, "/p/", MM_FILENAME_DIRECTORY); input = string(&f, &input_buffer, "child");
        f.status = MM_FILENAME_MISSING; before = f.mkdir_calls;
        CHECK(!mm_filename_down(&n->value, &f.host, &input, 0, &a->value) && f.mkdir_calls == before); equals(n, "/p/");
        CHECK(mm_filename_down(&n->value, &f.host, &input, MM_FILENAME_CREATE_DIR, &a->value)); equals(n, "/p/");
        f.mkdir_ok = 0;
        CHECK(!mm_filename_down(&n->value, &f.host, &input, MM_FILENAME_CREATE_DIR | MM_FILENAME_MOVE_TO_DIR, &a->value));
        CHECK(f.last_issue == MM_FILENAME_DIR_CREATE && f.issue_owner == 0 && f.issue_type == MM_FILENAME_DIRECTORY); equals(n, "/p/");
        f.mkdir_ok = 1; f.status = MM_FILENAME_INFO_ERROR;
        CHECK(mm_filename_down(&n->value, &f.host, &input, MM_FILENAME_CREATE_DIR | MM_FILENAME_MOVE_TO_DIR, &a->value)); equals(n, "/p/child/");
        CHECK(!strcmp(f.input, "/p/child/"));
        output = string(&f, &output_buffer, "unchanged"); memcpy(f.reply, "/p", sizeof("/p"));
        CHECK(mm_filename_up(&n->value, &f.host, &output, 0)); equals(n, "/p/child/");
        CHECK(!strcmp(f.input, "/p/child/.."));
        CHECK(length(&output) == 5 && get(output.buffer, size, 1) == 'c');
        memcpy(f.reply, "/p/", sizeof("/p/"));
        CHECK(mm_filename_up(&n->value, &f.host, &output, 0) && length(&output) == 5);
        CHECK(mm_filename_up(&n->value, &f.host, NULL, MM_FILENAME_MOVE_TO_DIR)); equals(n, "/p/");
        output = string(&f, &output_buffer, "unchanged");
        CHECK(!mm_filename_up(&n->value, &f.host, &output, MM_FILENAME_MOVE_TO_DIR) && length(&output) == 9); equals(n, "/p/");
        f.full_fail = 1; before = f.issue_count[MM_FILENAME_GENERAL];
        CHECK(!mm_filename_up(&n->value, &f.host, &output, 0) && f.issue_count[MM_FILENAME_GENERAL] == before); equals(n, "/p/");
        memset(long_path, 'p', 254); long_path[0] = long_path[253] = '/'; long_path[254] = 0;
        set(&f, n, long_path, MM_FILENAME_DIRECTORY); before = f.full_calls;
        CHECK(!mm_filename_up(&n->value, &f.host, &output, 0) && f.full_calls == before && length(&output) == 9);
        input = string(&f, &input_buffer, "x"); before = f.mkdir_calls;
        CHECK(!mm_filename_down(&n->value, &f.host, &input, MM_FILENAME_CREATE_DIR | MM_FILENAME_MOVE_TO_DIR, &a->value));
        CHECK(f.mkdir_calls == before && f.last_issue == MM_FILENAME_GENERAL && f.issue_length == 254); equals(n, long_path);
        set(&f, n, "/p/", MM_FILENAME_DIRECTORY); input = string(&f, &input_buffer, "xx");
        put(input.buffer, size, 1, size == 1 ? 0xe9U : 0x3a9U); f.status = MM_FILENAME_FOUND; f.directory = 1;
        before = f.mkdir_calls;
        CHECK(mm_filename_down(&n->value, &f.host, &input, MM_FILENAME_MOVE_TO_DIR, &a->value));
        CHECK(f.mkdir_calls == before && get(n->buffer.bytes, size, 4) == (size == 1 ? 0xe9U : 0x3a9U));
        f.host.value.validate = NULL; f.host.issue = NULL; f.host.assertion = NULL; f.host.copy_name = NULL;
        f.full_fail = 0; memcpy(f.reply, "/p/leaf.bin", sizeof("/p/leaf.bin")); input = string(&f, &input_buffer, "leaf.bin");
        CHECK(mm_filename_build(&n->value, &f.host, &input, MM_FILENAME_NIL)); equals(n, "/p/leaf.bin");
        f.full_fail = 1; CHECK(!mm_filename_build(&n->value, &f.host, &input, MM_FILENAME_NIL));
        CHECK(n->state.type == MM_FILENAME_NIL && !length(&n->value.path));
        CHECK(f.copies > 0 && f.copy_names > 0 && f.validations > 0);
    }
    puts("C89 filename paths: build/search, status priority, native ordering, navigation and failure publication passed");
    return 0;
}
