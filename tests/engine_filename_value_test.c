#include "mm/filename_value.h"
#include "mm/wide.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{ if (!value) { fprintf(stderr, "Filename value check failed at %d\n", line); exit(1); } }
#define CHECK(x) check(!!(x), __LINE__)
typedef union Buffer { unsigned char bytes[257]; unsigned short words[257]; } Buffer;
typedef struct Fixture {
    MMFilenameState state;
    Buffer path;
    MMFilenameValue value;
    MMFilenameValueHost host;
    unsigned validations, assertions, copies, failures, upper_calls, upper_order[8];
    int last_flags, last_assertion;
    MMI32 failure_type, failure_length;
} Fixture;
static unsigned get(const Buffer *p, unsigned size, int i) { return size == 1 ? p->bytes[i] : p->words[i]; }
static void put(Buffer *p, unsigned size, int i, unsigned ch)
{ if (size == 1) p->bytes[i] = (unsigned char)ch; else p->words[i] = (unsigned short)ch; }
static int length(Fixture *f) { return (int)get(&f->path, f->value.path.unit, 0); }
static unsigned upper(void *c, unsigned ch)
{
    Fixture *f = (Fixture *)c;
    if (f->upper_calls < 8) f->upper_order[f->upper_calls] = ch;
    ++f->upper_calls;
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 'A';
    if (ch == 0xe9) return 0xc9;
    return ch;
}
static int separator(void *c, unsigned ch) { (void)c; return ch == '/' || ch == '\\'; }
static void validate(void *c, const MMFilenameValue *v, int flags)
{ Fixture *f = (Fixture *)c; CHECK(v->state == &f->state); ++f->validations; f->last_flags = flags; }
static void assertion(void *c, int code) { Fixture *f = (Fixture *)c; ++f->assertions; f->last_assertion = code; }
static void failed(void *c)
{
    Fixture *f = (Fixture *)c; ++f->failures;
    f->failure_type = f->state.type; f->failure_length = length(f);
    CHECK(f->last_flags == MM_FILENAME_EMPTY);
}
static void copy(void *c, const void *source, void *destination, size_t bytes)
{ Fixture *f = (Fixture *)c; CHECK(source && destination && bytes > 0); ++f->copies; }
static void init(Fixture *f, unsigned size)
{
    memset(f, 0, sizeof(*f)); f->value.state = &f->state; f->value.owner = f;
    f->value.path.buffer = &f->path; f->value.path.unit = size;
    f->value.path.context = f; f->value.path.check_copy = copy;
    f->host.context = f; f->host.upper = upper; f->host.separator = separator;
    f->host.validate = validate; f->host.assertion = assertion; f->host.failed = failed;
    mm_filename_value_clear(&f->value, &f->host);
    CHECK(f->state.type == MM_FILENAME_NIL && !length(f) && f->last_flags == MM_FILENAME_EMPTY);
}
static void path(Fixture *f, const char *text)
{
    int i, count = (int)strlen(text); CHECK(count <= 255);
    put(&f->path, f->value.path.unit, 0, (unsigned)count);
    for (i = 0; i <= count; ++i) put(&f->path, f->value.path.unit, i + 1, (unsigned char)text[i]);
    mm_filename_derive_type(&f->value, &f->host);
}
static void equal(Fixture *f, const char *text)
{
    int i, count = (int)strlen(text); CHECK(length(f) == count);
    for (i = 0; i <= count; ++i) CHECK(get(&f->path, f->value.path.unit, i + 1) == (unsigned char)text[i]);
}
int main(void)
{
    Fixture f;
    Buffer extension, leaf, before;
    MMStringValue out;
    MMI32 type;
    MMU32 ch;
    unsigned size, expected;
    int i, count;
    char long_path[257];
    const char *paths[] = {"/p/plain", "/p/a.txt", "/p/a.abcd", "/p/a.abcde", "/p/a.", "/p/.ext", "/p/a.b.c", "/p/a.a b", "/p/a.1!@#", "/p/"};
    const MMI32 types[] = {0, 0x545854L, 0x41424344L, 0, 0, 0x455854L, 'C', 0x412042L, 0x31214023L, MM_FILENAME_DIRECTORY};
    for (size = 1; size <= 2; ++size) {
        init(&f, size); out = f.value.path; out.buffer = &extension;
        for (i = 0; i < 10; ++i) {
            path(&f, paths[i]); CHECK(f.state.type == types[i]);
            mm_filename_get_leaf(&f.value, &f.host, &out);
            count = (int)strlen(strrchr(paths[i], '/') + 1);
            CHECK(get(&extension, size, 0) == (unsigned)count && get(&extension, size, count + 1) == 0);
        }
        f.upper_calls = 0;
        type = mm_filename_clean_type(0x61626364L, &f.host, &out);
        CHECK(type == 0x41424344L && get(&extension, size, 0) == 4 && get(&extension, size, 1) == 'A');
        CHECK(f.upper_calls == 4 && f.upper_order[0] == 'd' && f.upper_order[3] == 'a');
        CHECK(mm_filename_clean_type(0x61006263L, &f.host, &out) == 0x4243L && get(&extension, size, 0) == 2);
        CHECK(mm_filename_clean_type(0x61626300L, &f.host, &out) == 0 && get(&extension, size, 0) == 0);
        CHECK(mm_filename_clean_type(MM_FILENAME_DIRECTORY, &f.host, &out) == MM_FILENAME_DIRECTORY && !get(&extension, size, 0));
        CHECK(mm_filename_clean_type(MM_FILENAME_NIL, &f.host, NULL) == MM_FILENAME_NIL);
        CHECK((MMU32)mm_filename_clean_type(mm_signed_word(0xe978797aUL), &f.host, &out) == 0xc958595aUL);
        /* Exercise every unit value against the native signed-byte round trip. */
        path(&f, "/x.q");
        for (ch = 0; ch < (size == 1 ? 256UL : 65536UL); ++ch) {
            put(&f.path, size, 4, (unsigned)ch);
            expected = size == 1 || ch < 128 || ch >= 0xff80UL ? 2 : 0;
            if (ch == '.') expected = 1;
            if (ch == '/' || ch == '\\') expected = 0;
            CHECK(mm_filename_extension_length(&f.value, &f.host) == (MMI32)expected);
        }
        put(&f.path, size, 4, size == 1 ? 0xe9U : 0xffe9U);
        mm_filename_derive_type(&f.value, &f.host); CHECK(f.state.type == 0xc9);
        if (size == 2) {
            put(&f.path, size, 4, 0xe9); mm_filename_derive_type(&f.value, &f.host); CHECK(!f.state.type);
        }
        path(&f, "/x.qqqq"); put(&f.path, size, 4, size == 1 ? 0xe9U : 0xffe9U);
        mm_filename_derive_type(&f.value, &f.host); CHECK((MMU32)f.state.type == 0xc9515151UL);
        path(&f, "/p/a.txt"); CHECK(mm_filename_change_type(&f.value, &f.host, 0x61626364L)); equal(&f, "/p/a.ABCD");
        CHECK(mm_filename_change_type(&f.value, &f.host, 0)); equal(&f, "/p/a");
        CHECK(mm_filename_set_leaf(&f.value, &f.host, NULL, 0, MM_FILENAME_DIRECTORY)); equal(&f, "/p/");
        for (i = 0; i < 257; ++i) put(&leaf, size, i, 'q');
        CHECK(mm_filename_set_leaf(&f.value, &f.host, &leaf, 4, MM_FILENAME_NIL)); equal(&f, "/p/qqqq");
        /* Exactly 255 units are possible when the extension and dot fit. */
        long_path[0] = '/'; memset(long_path + 1, 'q', 249); long_path[250] = 0;
        path(&f, long_path); CHECK(mm_filename_change_type(&f.value, &f.host, 0x41424344L) && length(&f) == 255);
        memset(long_path + 1, 'q', 254); long_path[251] = 0;
        path(&f, long_path); before = f.path;
        CHECK(!mm_filename_change_type(&f.value, &f.host, 0x41424344L) && !memcmp(&before, &f.path, sizeof(before)) && !f.state.type);
        memset(long_path + 1, 'q', 254); long_path[255] = 0;
        path(&f, long_path); CHECK(mm_filename_change_type(&f.value, &f.host, 0));
        f.state.type = 'X'; before = f.path;
        CHECK(!mm_filename_change_type(&f.value, &f.host, 0) && f.state.type == 'X' && !memcmp(&before, &f.path, sizeof(before)));
        /* Inner overflow publishes a parent; public set-leaf clears it first. */
        memset(long_path + 1, 'p', 98); long_path[99] = '/'; long_path[100] = 0;
        path(&f, long_path);
        CHECK(!mm_filename_change_leaf(&f.value, &f.host, &leaf, 156)); equal(&f, long_path);
        CHECK(f.state.type == MM_FILENAME_DIRECTORY && !f.failures);
        CHECK(!mm_filename_set_leaf(&f.value, &f.host, &leaf, 156, MM_FILENAME_NIL));
        CHECK(f.failures == 1 && f.failure_type == MM_FILENAME_NIL && f.failure_length == 0);
        path(&f, "/p/a.txt");
        CHECK(mm_filename_set_leaf(&f.value, &f.host, &leaf, 3, MM_FILENAME_DIRECTORY));
        CHECK(f.last_assertion == MM_FILENAME_LEAF_TYPE);
        path(&f, "a.txt"); mm_filename_get_leaf(&f.value, &f.host, &out);
        CHECK(f.last_assertion == MM_FILENAME_BAD_PATH && get(&extension, size, 0) == 5);
        path(&f, "/p/a.txt"); put(&f.path, size, 5, 0);
        mm_filename_get_leaf(&f.value, &f.host, &out); CHECK(get(&extension, size, 0) == 1);
        CHECK(f.copies > 0 && f.validations > 0);
        f.host.validate = NULL; f.host.assertion = NULL; f.host.failed = NULL;
        path(&f, "/p/a.txt"); CHECK(mm_filename_change_type(&f.value, &f.host, 'B')); equal(&f, "/p/a.B");
    }
    puts("C89 filename values: types, all byte/wide extension units, capacity, partial publication and callbacks passed");
    return 0;
}
