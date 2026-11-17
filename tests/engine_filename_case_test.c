#include "mm/filename_case.h"
#include "mm/string_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line) { if (!ok) { fprintf(stderr,"Filename case: %d\n",line); exit(1); } }
#define CHECK(x) check(!!(x),__LINE__)
typedef union Buffer { unsigned char bytes[257]; unsigned short words[257]; } Buffer;
typedef struct Fixture {
    MMFilenameCaseHost host; MMFilenameState state; MMFilenameValue value; Buffer buffer;
    const char *names[4]; const char *queries[4]; int statuses[4];
    unsigned size, query, entries, index, firsts, nexts, closes, lengths, copies, validations;
    unsigned input_lead, entry_lead;
    int end, handle, errors, last_flags;
} Fixture;
static unsigned get(const void *p, unsigned size, int i)
{ return size == 1 ? ((const unsigned char *)p)[i] : ((const unsigned short *)p)[i]; }
static void put(void *p, unsigned size, int i, unsigned ch)
{ if (size == 1) ((unsigned char *)p)[i] = (unsigned char)ch; else ((unsigned short *)p)[i] = (unsigned short)ch; }
static void set(Fixture *f, const char *s)
{
    Buffer b; int i, count = (int)strlen(s); CHECK(count <= 255);
    for (i = 0; i <= count; ++i) put(&b,f->size,i,(unsigned char)s[i]);
    mm_string_value_set(&f->value.path,&b,count);
}
static void same(const void *p, unsigned size, const char *s)
{ int i; for (i = 0; s[i]; ++i) CHECK(get(p,size,i) == (unsigned char)s[i]); CHECK(!get(p,size,i)); }
static unsigned upper(void *c, unsigned ch) { (void)c; return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch; }
static int separator(void *c, unsigned ch) { (void)c; return ch == '/'; }
static void validate(void *c, const MMFilenameValue *v, int flags)
{ Fixture *f = (Fixture *)c; CHECK(v == &f->value); ++f->validations; f->last_flags = flags; }
static void issue(void *c, const MMFilenameValue *v, int code)
{ Fixture *f = (Fixture *)c; (void)v; (void)code; ++f->errors; }
static void copy(void *c, const void *a, void *b, size_t bytes)
{ Fixture *f = (Fixture *)c; CHECK(a && b && bytes > 0); ++f->copies; }
static int info(void *c, const void *raw, MMFilenameInfo *out)
{
    Fixture *f = (Fixture *)c; unsigned index = f->query++; CHECK(index < 4 && f->queries[index]);
    if (f->input_lead) {
        CHECK(get(raw,f->size,0) == '/' && get(raw,f->size,1) == f->input_lead);
        same((const unsigned char *)raw + 2 * f->size,f->size,f->queries[index] + 2);
    } else same(raw,f->size,f->queries[index]);
    out->directory = out->hidden = out->system = 1; return f->statuses[index];
}
static int emit(Fixture *f, void *raw, MMI32 capacity)
{
    int i, count; const char *s;
    CHECK(capacity == 1024);
    if (f->index >= f->entries) return f->end;
    s = f->names[f->index]; count = (int)strlen(s); CHECK(count < capacity);
    for (i = 0; i <= count; ++i) put(raw,f->size,i,(unsigned char)s[i]);
    if (f->entry_lead) put(raw,f->size,0,f->entry_lead);
    return MM_FILENAME_CASE_ENTRY;
}
static int first(void *c, const void *pattern, void **iterator, void *name, MMI32 capacity)
{
    Fixture *f = (Fixture *)c; ++f->firsts; f->index = 0; same(pattern,f->size,"/*");
    *iterator = f->handle ? f : NULL; return emit(f,name,capacity);
}
static int next(void *c, void *iterator, void *name, MMI32 capacity)
{ Fixture *f = (Fixture *)c; CHECK(iterator == f); ++f->nexts; ++f->index; return emit(f,name,capacity); }
static void close_directory(void *c, void *iterator)
{ Fixture *f = (Fixture *)c; CHECK(iterator == (f->handle ? f : NULL)); ++f->closes; }
static MMI32 name_length(void *c, const void *name)
{ Fixture *f = (Fixture *)c; ++f->lengths; return (MMI32)mm_string_zero_length(name,f->size); }
static void init(Fixture *f, unsigned size)
{
    memset(f,0,sizeof(*f)); f->size = size; f->handle = 1; f->end = MM_FILENAME_CASE_END;
    f->state.type = 0x414243L; f->value.state = &f->state; f->value.owner = f;
    f->value.path.buffer = &f->buffer; f->value.path.unit = size; f->value.path.context = f; f->value.path.check_copy = copy;
    f->host.sensitive = 1; f->host.path.context = f->host.path.value.context = f;
    f->host.path.info = info; f->host.path.issue = issue; f->host.path.value.upper = upper;
    f->host.path.value.separator = separator; f->host.path.value.validate = validate;
    f->host.first = first; f->host.next = next; f->host.close = close_directory; f->host.name_length = name_length;
    f->queries[0] = f->queries[1] = "/FILE.BIN"; f->statuses[0] = f->statuses[1] = MM_FILENAME_MISSING;
    set(f,"/FILE.BIN");
}
static int run(Fixture *f) { int result = mm_filename_resolve_case(&f->value,&f->host); CHECK(!f->errors); return result; }
static void unchanged(Fixture *f)
{ same((const unsigned char *)f->value.path.buffer + f->size,f->size,"/FILE.BIN"); CHECK(f->state.type == 0x414243L); }
int main(void)
{
    Fixture f; unsigned size; char long_name[1024], max_path[256], max_name[255]; int i;
    for (size = 1; size <= 2; ++size) {
        init(&f,size); f.statuses[0] = MM_FILENAME_FOUND; f.host.first = NULL; CHECK(run(&f)); unchanged(&f); CHECK(f.query == 1);
        init(&f,size); f.statuses[0] = MM_FILENAME_INFO_ERROR; CHECK(!run(&f)); unchanged(&f); CHECK(f.query == 1);
        init(&f,size); f.host.sensitive = 0; f.host.first = NULL; CHECK(!run(&f)); unchanged(&f); CHECK(f.query == 1);
        init(&f,size); set(&f,"FILE.BIN"); f.queries[0] = "FILE.BIN"; CHECK(!run(&f) && f.query == 1 && !f.firsts);
        init(&f,size); set(&f,""); f.queries[0] = ""; CHECK(!run(&f) && f.query == 1 && !f.firsts);
        init(&f,size); set(&f,"/"); f.queries[0] = "/"; CHECK(run(&f) && f.state.type == MM_FILENAME_DIRECTORY && !f.firsts);
        init(&f,size); f.statuses[1] = MM_FILENAME_INFO_ERROR; CHECK(!run(&f)); unchanged(&f); CHECK(f.query == 2 && !f.firsts);
        init(&f,size); f.statuses[1] = MM_FILENAME_FOUND; CHECK(run(&f));
        CHECK(f.state.type == 0x42494eL && !f.firsts && f.last_flags == (MM_FILENAME_FILE | MM_FILENAME_DIR));
        init(&f,size); f.names[0] = "other"; f.names[1] = "file.bin"; f.entries = 2; CHECK(run(&f));
        same((const unsigned char *)f.value.path.buffer + size,size,"/file.bin");
        CHECK(f.state.type == 0x42494eL && f.firsts == 1 && f.nexts == 2 && f.closes == 1 && f.lengths == 2 && f.copies > 0);
        init(&f,size); f.names[0] = "file.bin"; f.names[1] = "FILE.BIN"; f.names[2] = "never read"; f.entries = 3;
        CHECK(!run(&f)); unchanged(&f); CHECK(f.nexts == 1 && f.closes == 1 && f.lengths == 2);
        init(&f,size); f.names[0] = "file.bin"; f.entries = 1; f.end = MM_FILENAME_CASE_ERROR;
        CHECK(!run(&f)); unchanged(&f); CHECK(f.nexts == 1 && f.closes == 1);
        for (i = 0; i < 2; ++i) {
            init(&f,size); f.handle = i; f.end = MM_FILENAME_CASE_ERROR;
            CHECK(!run(&f)); unchanged(&f); CHECK(f.closes == 1 && !f.nexts);
        }
        init(&f,size); f.names[0] = "unmatched"; f.entries = 1; CHECK(!run(&f)); unchanged(&f); CHECK(f.closes == 1);
        memset(long_name,'F',1023); long_name[1023] = 0;
        init(&f,size); f.names[0] = long_name; f.names[1] = "file.bin"; f.entries = 2;
        f.host.name_length = NULL; CHECK(run(&f) && f.closes == 1 && !f.lengths);
        max_path[0] = '/'; memset(max_path + 1,'a',254); max_path[255] = 0;
        memset(max_name,'A',254); max_name[254] = 0;
        init(&f,size); set(&f,max_path); f.queries[0] = f.queries[1] = max_path;
        f.names[0] = long_name; f.names[1] = max_name; f.entries = 2;
        CHECK(run(&f) && get(f.value.path.buffer,size,0) == 255 && f.state.type == 0);
        for (i = 0; i < 254; ++i) CHECK(get(f.value.path.buffer,size,i + 2) == 'A');
        init(&f,size); f.names[0] = "file.bin"; f.entries = 1;
        f.host.name_length = NULL; f.host.path.value.validate = NULL; f.host.path.issue = NULL; f.value.path.check_copy = NULL;
        CHECK(run(&f) && f.closes == 1 && !f.lengths);
        init(&f,size); f.input_lead = f.entry_lead = size == 1 ? 0xc0U : 0x3a9U;
        put(f.value.path.buffer,size,2,f.input_lead); f.names[0] = "file.bin"; f.entries = 1;
        CHECK(run(&f) && f.closes == 1 && get(f.value.path.buffer,size,2) == f.input_lead);
        init(&f,size); f.input_lead = size == 1 ? 0xc0U : 0x3a9U; f.entry_lead = size == 1 ? 0xe0U : 0x3c9U;
        put(f.value.path.buffer,size,2,f.input_lead); f.names[0] = "file.bin"; f.entries = 1;
        CHECK(!run(&f) && f.closes == 1 && f.state.type == 0x414243L && get(f.value.path.buffer,size,2) == f.input_lead);
    }
    puts("C89 filename case: fast paths, ASCII matching, ambiguity, iterator lifetime and unchanged failure passed");
    return 0;
}
