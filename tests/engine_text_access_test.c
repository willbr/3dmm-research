#include "mm/text_access.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "text access failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check(!!(x), __LINE__)
typedef struct Host
{
    MMTextAccessServices s;
    MMTextCache cache;
    unsigned short storage[512], alternate[512], text[4096];
    MMI32 characters, fetch_at, fetch_bytes;
    int fetches, caches, reads, lowers, patterns, diagnostics[2], fail_fetch, fail_cache, fail_read, fail_lower,
        fail_pattern, result, alternate_cache;
} Host;
static MMU32 at(const void *p, MMI32 i, unsigned unit)
{
    unsigned short v;
    if (unit == 1)
        return ((const unsigned char *)p)[i];
    memcpy(&v, (const unsigned char *)p + 2 * i, 2);
    return v;
}
static void store(void *p, MMI32 i, unsigned unit, MMU32 v)
{
    unsigned short w = (unsigned short)v;
    if (unit == 1)
        ((unsigned char *)p)[i] = (unsigned char)v;
    else
        memcpy((unsigned char *)p + 2 * i, &w, 2);
}
static unsigned classify(void *v, MMU32 ch)
{
    (void)v;
    return ch == '\r'                ? MM_TEXT_CHAR_BREAK | MM_TEXT_CHAR_MAY_BREAK
           : ch == '\n'              ? MM_TEXT_CHAR_IGNORE
           : ch == ' ' || ch == '\t' ? MM_TEXT_CHAR_MAY_BREAK
                                     : 0;
}
static MMU32 lower_value(MMU32 ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch == 0x141 ? 0x161 : ch;
}
static void view(void *v, MMTextCache *out)
{
    *out = ((Host *)v)->cache;
}
static int fetch(void *v, MMI32 offset, MMI32 bytes, void *out)
{
    Host *h = (Host *)v;
    CHECK(offset >= 0 && bytes >= 0 && offset + bytes <= h->characters * (MMI32)h->cache.unit);
    h->fetch_at = offset;
    h->fetch_bytes = bytes;
    if (++h->fetches == h->fail_fetch)
    {
        if (bytes)
            memcpy(out, (const unsigned char *)h->text + offset, h->cache.unit);
        return h->result;
    }
    memcpy(out, (const unsigned char *)h->text + offset, (size_t)bytes);
    return 1;
}
static int cache(void *v, MMI32 first, MMI32 end)
{
    Host *h = (Host *)v;
    MMI32 i;
    if (++h->caches == h->fail_cache)
        return h->result;
    if (h->alternate_cache)
    {
        h->cache.data = h->alternate;
        h->cache.first = first > 2 ? first - 2 : 0;
        h->cache.end = end;
        for (i = h->cache.first; i < end; ++i)
            store(h->cache.data, i - h->cache.first, h->cache.unit, at(h->text, i, h->cache.unit));
        return 1;
    }
    return mm_text_cache_fill(&h->cache, first, end, h->characters, &h->s);
}
static int character(void *v, MMI32 cp, MMU32 *out)
{
    Host *h = (Host *)v;
    CHECK(cp >= 0 && cp < h->characters);
    if (++h->reads == h->fail_read)
        return h->result;
    return mm_text_access_character(cp, out, &h->s);
}
static int lower(void *v, MMU32 ch, MMU32 *out)
{
    Host *h = (Host *)v;
    if (++h->lowers == h->fail_lower)
        return h->result;
    *out = lower_value(ch);
    return 1;
}
static int lower_pattern(void *v, void *p, MMI32 count)
{
    Host *h = (Host *)v;
    MMI32 i;
    if (++h->patterns == h->fail_pattern)
        return h->result;
    for (i = 0; i < count; ++i)
        store(p, i, h->cache.unit, lower_value(at(p, i, h->cache.unit)));
    return 1;
}
static void diagnostic(void *v, int kind)
{
    Host *h = (Host *)v;
    CHECK(kind == MM_TEXT_ISOLATED_LF || kind == MM_TEXT_NO_FINAL_CR);
    ++h->diagnostics[kind == MM_TEXT_NO_FINAL_CR];
}
static void init(Host *h, unsigned unit)
{
    MMI32 i;
    const unsigned char pattern[] = {'A', 'b', ' ', '\r', '\n', '\n', 'X', '\t', 0, 'q'};
    memset(h, 0, sizeof(*h));
    h->cache.data = h->storage;
    h->cache.unit = unit;
    h->characters = 1800;
    h->result = -1;
    for (i = 0; i < h->characters; ++i)
        store(h->text, i, unit, pattern[i % 10]);
    store(h->text, h->characters - 1, unit, '\r');
    h->s.context = h;
    h->s.view = view;
    h->s.fetch = fetch;
    h->s.cache = cache;
    h->s.character = character;
    h->s.classify = classify;
    h->s.lower = lower;
    h->s.lower_pattern = lower_pattern;
    h->s.diagnostic = diagnostic;
}
static void contents(Host *h)
{
    MMI32 i;
    CHECK(h->cache.first >= 0 && h->cache.end >= h->cache.first && h->cache.end <= h->characters &&
          h->cache.end - h->cache.first <= 512);
    for (i = h->cache.first; i < h->cache.end; ++i)
        CHECK(at(h->cache.data, i - h->cache.first, h->cache.unit) == at(h->text, i, h->cache.unit));
}
static MMU32 random_word(MMU32 *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}
static void fills(void)
{
    Host h;
    unsigned unit;
    MMI32 a, b, i;
    MMU32 state = 2026, ch;
    int failure;
    for (unit = 1; unit <= 2; ++unit)
    {
        init(&h, unit);
        CHECK(cache(&h, 0, 1) == 1 && h.cache.first == 0 && h.cache.end == 512 && h.fetches == 1);
        CHECK(cache(&h, 20, 40) == 1 && h.fetches == 1);
        CHECK(cache(&h, 500, 700) == 1 && h.cache.first == 188 && h.cache.end == 700 &&
              h.fetch_at == 512 * (MMI32)unit && h.fetch_bytes == 188 * (MMI32)unit);
        contents(&h);
        CHECK(cache(&h, 1000, 1001) == 1 && h.cache.first == 489 && h.cache.end == 1001);
        contents(&h);
        CHECK(cache(&h, 1, 30) == 1 && h.cache.first == 1 && h.cache.end == 513 && h.fetch_at == (MMI32)unit &&
              h.fetch_bytes == 488 * (MMI32)unit);
        contents(&h);
        for (i = 0; i < 1000; ++i)
        {
            a = (MMI32)(random_word(&state) % 1800);
            b = a + 1 + (MMI32)(random_word(&state) % (MMU32)(h.characters - a));
            if (b - a > 512)
                b = a + 512;
            CHECK(cache(&h, a, b) == 1 && h.cache.first <= a && h.cache.end >= b);
            contents(&h);
        }
        for (failure = 0; failure >= -2; --failure)
        {
            init(&h, unit);
            h.fail_fetch = 1;
            h.result = failure;
            CHECK(cache(&h, 1000, 1001) == failure && h.cache.first == 744 && h.cache.end == 1256);
            init(&h, unit);
            CHECK(cache(&h, 1000, 1001) == 1);
            h.fail_fetch = 2;
            h.result = failure;
            CHECK(cache(&h, 500, 501) == failure && h.cache.first == 744 && h.cache.end == 1012);
            init(&h, unit);
            CHECK(cache(&h, 0, 1) == 1);
            h.fail_fetch = 2;
            h.result = failure;
            CHECK(cache(&h, 600, 601) == failure && h.cache.first == 0 && h.cache.end == 512);
        }
        init(&h, unit);
        h.alternate_cache = 1;
        CHECK(mm_text_access_character(17, &ch, &h.s) == 1 && ch == at(h.text, 17, unit) &&
              h.cache.data == h.alternate);
        {
            unsigned short out[700];
            CHECK(mm_text_access_fetch(0, 700, out, &h.s) == 1 && !memcmp(out, h.text, 700 * unit));
            init(&h, unit);
            memset(out, 0xa3, sizeof(out));
            h.fail_cache = 2;
            CHECK(mm_text_access_fetch(0, 700, out, &h.s) == -1 && !memcmp(out, h.text, 512 * unit));
            CHECK(((unsigned char *)out)[512 * unit] == 0xa3);
            h.caches = 0;
            h.fail_cache = 1;
            ch = 123;
            h.cache.first = h.cache.end = 0;
            CHECK(mm_text_access_character(17, &ch, &h.s) == -1 && ch == 123);
        }
    }
}
static void invalidations(void)
{
    Host h;
    unsigned unit;
    MMI32 cp, ins, del, old_first, old_end, front, back, expected_first, expected_end, i;
    MMU32 state = 17;
    for (unit = 1; unit <= 2; ++unit)
        for (i = 0; i < 800; ++i)
        {
            init(&h, unit);
            cp = (MMI32)(random_word(&state) % 1800);
            CHECK(cache(&h, cp, cp + 1) == 1);
            old_first = h.cache.first;
            old_end = h.cache.end;
            cp = (MMI32)(random_word(&state) % 1800);
            del = (MMI32)(random_word(&state) % 60);
            if (del >= 1800 - cp)
                del = 1799 - cp;
            ins = (MMI32)(random_word(&state) % 32);
            expected_first = old_first;
            expected_end = old_end;
            if (old_end > cp && (ins || del))
            {
                if (old_first >= cp + del)
                {
                    expected_first += ins - del;
                    expected_end += ins - del;
                }
                else
                {
                    front = cp > old_first ? cp - old_first : 0;
                    back = old_end > cp + del ? old_end - cp - del : 0;
                    if (!front && !back)
                        expected_first = expected_end = 0;
                    else if (front >= back)
                        expected_end = cp;
                    else
                    {
                        expected_first = cp + ins;
                        expected_end = expected_first + back;
                    }
                }
            }
            memmove((unsigned char *)h.text + (cp + ins) * unit, (unsigned char *)h.text + (cp + del) * unit,
                    (size_t)(1800 - cp - del) * unit);
            memset((unsigned char *)h.text + cp * unit, 0, (size_t)ins * unit);
            h.characters += ins - del;
            mm_text_cache_invalidate(&h.cache, cp, ins, del);
            CHECK(h.cache.first == expected_first && h.cache.end == expected_end);
            contents(&h);
        }
}
static void navigation(void)
{
    Host h;
    unsigned unit;
    MMI32 starts[64], words[64], paragraphs = 0, word_count = 0, cp, i, first, end, previous, next, pword, nword, value;
    int j, failure, r;
    const unsigned char text[] = {'A', 'b', ' ', 'C', '\r', '\n', '\n', 'D', '\t', 'E', 0, 'f', '\r', '\r', 'Z', '\r'};
    for (unit = 1; unit <= 2; ++unit)
    {
        init(&h, unit);
        h.characters = sizeof(text);
        starts[0] = 0;
        paragraphs = 1;
        word_count = 0;
        for (cp = 0; cp < h.characters; ++cp)
            store(h.text, cp, unit, text[cp]);
        for (cp = 1; cp < h.characters; ++cp)
            if (text[cp] != '\n')
            {
                i = cp - 1;
                while (i >= 0 && text[i] == '\n')
                    --i;
                if (i >= 0 && text[i] == '\r')
                    starts[paragraphs++] = cp;
            }
        for (cp = 0; cp < h.characters; ++cp)
            if (!classify(&h, text[cp]) && (cp == 0 || classify(&h, text[cp - 1])))
                words[word_count++] = cp;
        for (cp = 0; cp <= h.characters; ++cp)
        {
            first = 0;
            end = h.characters;
            for (i = 0; i < paragraphs; ++i)
            {
                if (starts[i] <= (cp == h.characters ? cp - 1 : cp))
                    first = starts[i];
                else
                {
                    end = starts[i];
                    break;
                }
            }
            CHECK(mm_text_navigate(MM_TEXT_NAV_START, cp, 0, h.characters, &value, &h.s) == 1 && value == first);
            CHECK(mm_text_navigate(MM_TEXT_NAV_END, cp, 0, h.characters, &value, &h.s) == 1 &&
                  value == (cp == h.characters ? h.characters : end));
            CHECK(mm_text_navigate(MM_TEXT_NAV_IS_START, cp, 0, h.characters, &value, &h.s) == 1 &&
                  value == (cp == first && cp < h.characters));
            previous = cp;
            if (previous)
                do
                {
                    --previous;
                } while (previous > 0 && text[previous] == '\n');
            next = cp;
            if (next < h.characters)
            {
                ++next;
                while (next < h.characters && text[next] == '\n')
                    ++next;
            }
            pword = 0;
            nword = h.characters;
            for (i = 0; i < word_count; ++i)
            {
                if (words[i] < cp)
                    pword = words[i];
                if (words[i] > cp && nword == h.characters)
                    nword = words[i];
            }
            CHECK(mm_text_navigate(MM_TEXT_NAV_PREVIOUS, cp, 0, h.characters, &value, &h.s) == 1 && value == previous);
            CHECK(mm_text_navigate(MM_TEXT_NAV_NEXT, cp, 0, h.characters, &value, &h.s) == 1 && value == next);
            CHECK(mm_text_navigate(MM_TEXT_NAV_PREVIOUS, cp, 1, h.characters, &value, &h.s) == 1 && value == pword);
            CHECK(mm_text_navigate(MM_TEXT_NAV_NEXT, cp, 1, h.characters, &value, &h.s) == 1 && value == nword);
        }
        for (j = 0; j < 5; ++j)
            for (failure = 0; failure >= -2; --failure)
            {
                h.fail_read = h.reads + 1;
                h.result = failure;
                value = 123;
                r = mm_text_navigate(j, 3, 0, h.characters, &value, &h.s);
                CHECK(r == failure && value == 123);
            }
        init(&h, unit);
        h.characters = 4;
        store(h.text, 0, unit, '\n');
        store(h.text, 1, unit, '\n');
        store(h.text, 2, unit, 'A');
        store(h.text, 3, unit, '\r');
        CHECK(mm_text_navigate(MM_TEXT_NAV_IS_START, 2, 0, 4, &value, &h.s) == 1 && !value && h.diagnostics[0] == 1);
        h.cache.first = h.cache.end = 0;
        store(h.text, 3, unit, 'B');
        CHECK(mm_text_navigate(MM_TEXT_NAV_END, 2, 0, 4, &value, &h.s) == 1 && value == 4 && h.diagnostics[1] == 1);
    }
}
static int naive(Host *h, const void *pattern, MMI32 length, MMI32 first, int sensitive, MMI32 *found)
{
    MMI32 cp, i;
    MMU32 a, b;
    for (cp = first; cp + length <= h->characters; ++cp)
    {
        for (i = 0; i < length; ++i)
        {
            a = at(h->text, cp + i, h->cache.unit);
            b = at(pattern, i, h->cache.unit);
            if (!sensitive)
            {
                a = lower_value(a);
                b = lower_value(b);
            }
            if (a != b)
                break;
        }
        if (i == length)
        {
            *found = cp;
            return 1;
        }
    }
    return 0;
}
static void searches(void)
{
    Host h;
    unsigned unit;
    unsigned short pattern[12], original[12];
    MMU32 state = 91;
    MMI32 n, first, found, a, b, i, iteration;
    int sensitive, expected, r, failure;
    /* Wide values sharing a low byte remain bitmap candidates, but must not
     * compare equal. The coarse bitmap also preserves virtual read order. */
    init(&h, 2);
    h.characters = 5;
    store(h.text, 0, 2, 'B');
    store(h.text, 1, 2, 0x241);
    store(h.text, 2, 2, 0x141);
    store(h.text, 3, 2, 'X');
    store(h.text, 4, 2, '\r');
    store(pattern, 0, 2, 0x141);
    store(pattern, 1, 2, 'X');
    CHECK(mm_text_find(pattern, 2, 2, 0, 5, 1, &a, &b, &h.s) == 1 && a == 2 && b == 4 && h.reads == 4);
    for (unit = 1; unit <= 2; ++unit)
        for (iteration = 0; iteration < 400; ++iteration)
        {
            init(&h, unit);
            h.characters = 80;
            if (unit == 2)
            {
                store(h.text, 8, 2, 0x141);
                store(h.text, 18, 2, 0x241);
            }
            n = 1 + (MMI32)(random_word(&state) % 8);
            first = (MMI32)(random_word(&state) % 81);
            sensitive = (int)(random_word(&state) % 2);
            for (i = 0; i < n; ++i)
                store(pattern, i, unit, at(h.text, (iteration % 3 ? i : (MMI32)(random_word(&state) % 80)), unit));
            memcpy(original, pattern, (size_t)n * unit);
            expected = naive(&h, pattern, n, first, sensitive, &found);
            a = b = 123;
            r = mm_text_find(pattern, n, unit, first, h.characters, sensitive, &a, &b, &h.s);
            CHECK(r == expected && (r ? (a == found && b == found + n) : (a == 123 && b == 123)));
            for (i = 0; i < n; ++i)
                CHECK(at(pattern, i, unit) == (sensitive ? at(original, i, unit) : lower_value(at(original, i, unit))));
        }
    for (unit = 1; unit <= 2; ++unit)
        for (failure = 0; failure >= -2; --failure)
            for (i = 0; i < 3; ++i)
            {
                init(&h, unit);
                store(pattern, 0, unit, 'A');
                store(pattern, 1, unit, 'b');
                a = b = 123;
                h.result = failure;
                if (i == 0)
                    h.fail_pattern = 1;
                else if (i == 1)
                    h.fail_read = 1;
                else
                    h.fail_lower = 1;
                CHECK(mm_text_find(pattern, 2, unit, 0, h.characters, 0, &a, &b, &h.s) == failure && a == 123 &&
                      b == 123);
            }
}
int main(void)
{
    fills();
    invalidations();
    navigation();
    searches();
    puts("Text access checks passed");
    return 0;
}
