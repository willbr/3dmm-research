#include "three_mm/movie_title.h"
#include "three_mm/movie_root.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "movie title/root failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
static void title_set(ThreeMMMovieTitle *t, const char *s)
{
    unsigned short wide[256];
    size_t i, n = strlen(s);
    CHECK(n <= 255);
    for (i = 0; i < n; ++i)
        wide[i] = (unsigned char)s[i];
    CHECK(three_mm_movie_title_set(t, t->unit == 1 ? (const void *)s : wide, (ThreeMMU32)n));
}
static int title_is(const ThreeMMMovieTitle *t, const char *s)
{
    size_t i, n = strlen(s);
    const unsigned char *bytes = (const unsigned char *)three_mm_movie_title_text(t);
    unsigned short value;
    if (t->count != n)
        return 0;
    for (i = 0; i < n; ++i)
    {
        if (t->unit == 1)
            value = bytes[i];
        else
            memcpy(&value, bytes + i * 2, 2);
        if (value != (unsigned char)s[i])
            return 0;
    }
    if (t->unit == 1)
        return bytes[n] == 0;
    memcpy(&value, bytes + n * 2, 2);
    return value == 0;
}
typedef struct TitleHost
{
    char log[16];
    int used, mutate, empty_default;
    const char *leaf;
    ThreeMMMovieTitle *current;
} TitleHost;
static void event(TitleHost *h, char c)
{
    CHECK(h->used < 15);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static void leaf(void *c, ThreeMMMovieTitle *t)
{
    TitleHost *h = (TitleHost *)c;
    event(h, 'l');
    title_set(t, h->leaf);
}
static void fallback(void *c, ThreeMMMovieTitle *t)
{
    TitleHost *h = (TitleHost *)c;
    event(h, 'd');
    CHECK(t == h->current && !t->count);
    title_set(t, h->empty_default ? "" : "Default");
}
static void notify(void *c, ThreeMMMovieTitle *t)
{
    TitleHost *h = (TitleHost *)c;
    event(h, 'n');
    CHECK(t == h->current);
    if (h->mutate)
        title_set(t, "Changed by host");
}
static void titles(void)
{
    ThreeMMMovieTitle t, before;
    TitleHost h;
    ThreeMMMovieTitleServices s = {NULL, leaf, fallback, notify};
    const char *inputs[] = {"Movie.3mm", "Movie.part.3mm", ".hidden", "trailing.", "plain", "", "a.b.c", "a..b"};
    const char *expected[] = {"Movie", "Movie.part", "Default", "trailing", "plain", "Default", "a.b", "a."};
    char full[256];
    unsigned unit;
    size_t i;
    memset(&h, 0, sizeof(h));
    s.context = &h;
    h.current = &t;
    memset(full, 'x', 255);
    full[255] = 0;
    for (unit = 1; unit <= 2; ++unit)
    {
        CHECK(three_mm_movie_title_init(&t, unit));
        three_mm_movie_title_choose(&t, 0, &s);
        CHECK(title_is(&t, "Default") && !strcmp(h.log, "dn"));
        h.used = 0;
        h.log[0] = 0;
        three_mm_movie_title_choose(&t, 0, &s);
        CHECK(title_is(&t, "Default") && !h.used);
        for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
        {
            h.used = 0;
            h.log[0] = 0;
            h.leaf = inputs[i];
            three_mm_movie_title_choose(&t, 1, &s);
            CHECK(title_is(&t, expected[i]) && !strcmp(h.log, (i == 2 || i == 5) ? "ldn" : "ln"));
        }
        title_set(&t, "ABCDE");
        CHECK(three_mm_movie_title_set(&t, (const unsigned char *)three_mm_movie_title_text(&t) + unit, 3) &&
              title_is(&t, "BCD"));
        title_set(&t, full);
        before = t;
        CHECK(!three_mm_movie_title_set(&t, full, 256) && !memcmp(&before, &t, sizeof(t)));
        CHECK(!three_mm_movie_title_set(&t, NULL, 1) && !memcmp(&before, &t, sizeof(t)));
        CHECK(three_mm_movie_title_set(&t, NULL, 0) && title_is(&t, ""));
        h.mutate = 1;
        h.used = 0;
        h.log[0] = 0;
        three_mm_movie_title_choose(&t, 0, &s);
        CHECK(title_is(&t, "Changed by host") && !strcmp(h.log, "dn"));
        h.mutate = 0;
        h.empty_default = 1;
        CHECK(three_mm_movie_title_set(&t, NULL, 0));
        three_mm_movie_title_choose(&t, 0, &s);
        CHECK(!t.count);
        h.empty_default = 0;
        h.used = 0;
        h.log[0] = 0;
    }
    before = t;
    CHECK(!three_mm_movie_title_init(&t, 3) && !memcmp(&before, &t, sizeof(t)));
}
typedef struct RootHost
{
    char log[32];
    int used, first_fail, read_fail, io_fail, add_fail, write_fail, error;
    ThreeMMU32 *number;
    unsigned char bytes[8];
    ThreeMMU32 length;
} RootHost;
static void root_event(RootHost *h, char c)
{
    CHECK(h->used < 31);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int first(void *c, ThreeMMU32 *out)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'f');
    *out = 17;
    return !h->first_fail;
}
static int read_at(void *c, ThreeMMU32 offset, void *bytes, size_t size)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'i');
    if (h->io_fail)
        return 0;
    CHECK(offset + size <= 8);
    memcpy(bytes, h->bytes + offset, size);
    return 1;
}
static int read_root(void *c, ThreeMMU32 number, ThreeMMReadStream *s)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'r');
    CHECK(number == 17 || number == 99);
    s->context = h;
    s->length = h->length;
    s->read_at = read_at;
    return !h->read_fail;
}
static int add(void *c, ThreeMMU32 bytes, ThreeMMU32 *number)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'a');
    CHECK(bytes == 8);
    *number = 17;
    return !h->add_fail;
}
static int write_root(void *c, const void *bytes, ThreeMMU32 count)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'w');
    CHECK(count == 8);
    memcpy(h->bytes, bytes, 8);
    return !h->write_fail;
}
static void remove_root(void *c, ThreeMMU32 n)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'd');
    CHECK(n == 17 && h->number && *h->number == 17);
}
static void error(void *c, int result)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'e');
    h->error = result;
}
static void reset(RootHost *h)
{
    h->used = 0;
    h->log[0] = 0;
    h->error = 0;
}
static void roots(void)
{
    RootHost h;
    ThreeMMMovieRootServices s = {NULL, first, read_root, add, write_root, remove_root, error};
    ThreeMMMovieHeader header, unchanged;
    ThreeMMU32 number;
    int big;
    memset(&h, 0, sizeof(h));
    s.context = &h;
    h.number = &number;
    h.length = 8;
    for (big = 0; big < 2; ++big)
    {
        reset(&h);
        number = 99;
        CHECK(three_mm_movie_create_root(&number, 0x0303, big, &s) && number == 17 && !strcmp(h.log, "aw"));
        reset(&h);
        number = 0xffffffffUL;
        CHECK(three_mm_movie_verify_root(&number, &header, &s) && number == 17 && !strcmp(h.log, "fri"));
        CHECK(header.current_version == 2 && header.backward_version == 2 && header.system == 0x0303 &&
              header.big_endian == big);
        reset(&h);
        CHECK(three_mm_movie_verify_root(&number, NULL, &s) && !strcmp(h.log, "ri"));
        reset(&h);
        CHECK(three_mm_movie_verify_root(NULL, &header, &s) && !strcmp(h.log, "fri"));
        unchanged = header;
        h.read_fail = 1;
        number = 0xffffffffUL;
        reset(&h);
        CHECK(!three_mm_movie_verify_root(&number, &header, &s) && number == 17 && h.error == THREE_MM_MOVIE_BAD_FILE &&
              !memcmp(&header, &unchanged, sizeof(header)) && !strcmp(h.log, "fre"));
        h.read_fail = 0;
        h.first_fail = 1;
        number = 0xffffffffUL;
        reset(&h);
        CHECK(!three_mm_movie_verify_root(&number, &header, &s) && number == 0xffffffffUL && !strcmp(h.log, "fe"));
        h.first_fail = 0;
        h.io_fail = 1;
        number = 99;
        reset(&h);
        CHECK(!three_mm_movie_verify_root(&number, &header, &s) && number == 99 && h.error == THREE_MM_MOVIE_BAD_FILE &&
              !strcmp(h.log, "rie"));
        h.io_fail = 0;
        h.length = 7;
        reset(&h);
        CHECK(!three_mm_movie_verify_root(&number, &header, &s) && h.error == THREE_MM_MOVIE_BAD_FILE &&
              !strcmp(h.log, "re"));
        h.length = 8;
        h.bytes[big ? 5 : 4] = 0;
        reset(&h);
        number = 0xffffffffUL;
        CHECK(!three_mm_movie_verify_root(&number, &header, &s) && number == 17 &&
              h.error == THREE_MM_MOVIE_BAD_VERSION && !memcmp(&header, &unchanged, sizeof(header)));
        h.bytes[0] = h.bytes[1] = 0;
        reset(&h);
        CHECK(!three_mm_movie_verify_root(&number, &header, &s) && h.error == THREE_MM_MOVIE_BAD_FILE);
        reset(&h);
        h.add_fail = 1;
        number = 99;
        CHECK(!three_mm_movie_create_root(&number, 0x0303, big, &s) && number == 17 && !strcmp(h.log, "a"));
        h.add_fail = 0;
        reset(&h);
        h.write_fail = 1;
        number = 99;
        CHECK(!three_mm_movie_create_root(&number, 0x0303, big, &s) && number == 0xffffffffUL && !strcmp(h.log, "awd"));
        h.write_fail = 0;
    }
}
int main(void)
{
    titles();
    roots();
    puts("movie title/root passed");
    return 0;
}
