#include "text_format_test_host.h"
static unsigned byte_word(const unsigned char *p, unsigned n, int big)
{
    unsigned i, v = 0;
    for (i = 0; i < n; ++i)
        v |= (unsigned)p[big ? n - 1 - i : i] << (i * 8);
    return v;
}
static void deltas(void)
{
    Host h;
    MMTextCharacter p, old;
    MMTextParagraph pap, old_pap;
    MMU32 value, mask, expected;
    MMTextPropertyChange changes[4];
    MMI32 n;
    unsigned unit, i, j;
    int big, r, failure;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
        {
            init(&h, unit, big);
            p = h.input;
            p.font = 11;
            for (i = 0; i < 256; ++i)
            {
                p.style = i;
                p.height = 12 + (MMI32)(i * 257);
                p.offset = (MMI32)i - 128;
                old = p;
                old.style = 255 - i;
                if (i & 1)
                    ++old.height;
                if (i & 2)
                    ++old.offset;
                mask = 0;
                value = 0;
                CHECK(mm_text_character_delta(1, &h.config, &p, &old, &value, &mask, &h.edit.format) == 1);
                expected = ((i & 1) ? 0xffff0000UL : 0) | ((i & 2) ? 0xff00UL : 0) | (i ^ (255 - i));
                CHECK(mask == expected);
                CHECK(value == (((((i * 257UL) & 65535UL) << 16) | (((MMU32)p.offset & 255U) << 8) | i) & mask));
            }
            old = p;
            value = 123;
            CHECK(!mm_text_character_delta(1, &h.config, &p, &old, &value, &mask, &h.edit.format) && !mask &&
                  value == 123);
            for (i = 2; i <= 4; ++i)
            {
                value = 123;
                CHECK(!mm_text_character_delta(i, &h.config, &p, &p, &value, &mask, &h.edit.format) && !mask &&
                      value == 123);
                CHECK(mm_text_character_delta(i, &h.config, &p, 0, &value, &mask, &h.edit.format) == 1 &&
                      mask == 0xffffffffUL);
                CHECK(value == (i == 2 ? 0 : i == 3 ? p.foreground - h.config.black : p.background - h.config.clear));
            }
            p = h.input;
            for (j = 0; j < 3; ++j)
            {
                h.name_count = j == 0 ? 0 : j == 1 ? 3 : 255;
                h.length = 0;
                h.group = 0;
                CHECK(mm_text_character_delta(2, &h.config, &p, 0, &value, &mask, &h.edit.format) == 1 && value == 1);
                CHECK(h.length == (MMI32)(8 + (h.name_count + 2) * unit));
                CHECK(byte_word(h.bytes, 4, big) == 17 &&
                      byte_word(h.bytes + 4, 2, big) == (unsigned)h.config.text.system &&
                      byte_word(h.bytes + 6, 2, big) == (unsigned)h.config.text.system);
                CHECK(byte_word(h.bytes + 8, unit, big) == h.name_count);
                for (i = 0; i < h.name_count; ++i)
                    CHECK(byte_word(h.bytes + 8 + (i + 1) * unit, unit, big) == (i == 1 ? 0 : 'A' + i % 26));
                CHECK(!byte_word(h.bytes + h.length - unit, unit, big));
                mm_text_property_ref(2, 1, 0, &h.edit.format.properties.arguments);
                CHECK(h.free);
            }
            memset(&pap, 0, sizeof(pap));
            pap.tab = 18;
            pap.line = pap.after = 256;
            old_pap = pap;
            for (i = 128; i <= 130; ++i)
            {
                value = 123;
                CHECK(!mm_text_paragraph_delta(i, &h.config, &pap, &old_pap, &value, &mask) && !mask && value == 123);
                CHECK(mm_text_paragraph_delta(i, &h.config, &pap, 0, &value, &mask) && !value && mask == 0xffffffffUL);
            }
            pap.justification = 2;
            pap.indent = 3;
            pap.tab = -32768;
            pap.line = -32768;
            pap.extra_line = -7;
            pap.after = 32767;
            pap.extra_after = -32768;
            CHECK(mm_text_paragraph_delta(128, &h.config, &pap, &old_pap, &value, &mask) && mask == 0xffffffffUL &&
                  value == 0x02037feeUL);
            old_pap.justification = 2;
            CHECK(mm_text_paragraph_delta(128, &h.config, &pap, &old_pap, &value, &mask) && mask == 0x00ffffffUL);
            old_pap.indent = 3;
            CHECK(mm_text_paragraph_delta(128, &h.config, &pap, &old_pap, &value, &mask) && mask == 65535UL &&
                  value == 0x7feeUL);
            CHECK(mm_text_paragraph_delta(129, &h.config, &pap, 0, &value, &mask) && value == 0x7f00fff9UL);
            CHECK(mm_text_paragraph_delta(130, &h.config, &pap, 0, &value, &mask) && value == 0x7eff8000UL);
            init(&h, unit, big);
            p = h.input;
            CHECK(mm_text_format_changes(0, &h.config, &p, 0, changes, &n, &h.edit.format) == 1 && n == 4);
            for (i = 0; i < 4; ++i)
                CHECK(changes[i].kind == i + 1 && changes[i].mask == 0xffffffffUL);
            CHECK(h.fixed == 0x02000001UL);
            mm_text_property_ref(2, 1, 0, &h.edit.format.properties.arguments);
            CHECK(h.free);
            for (failure = 0; failure >= -2; --failure)
                for (i = 0; i < 3; ++i)
                {
                    init(&h, unit, big);
                    p = h.input;
                    h.fail = "FGA"[i];
                    h.result = failure;
                    value = 123;
                    r = mm_text_character_delta(2, &h.config, &p, 0, &value, &mask, &h.edit.format);
                    CHECK(r == (failure ? failure : MM_TEXT_FORMAT_FAILED) && value == 123 &&
                          mask == (failure ? 0xffffffffUL : 0));
                    init(&h, unit, big);
                    p = h.input;
                    h.fail = "FGA"[i];
                    h.result = failure;
                    CHECK(mm_text_format_changes(0, &h.config, &p, 0, changes, &n, &h.edit.format) == failure &&
                          n == 1 && changes[0].kind == 1);
                }
        }
}
static void entry(Host *h, unsigned kind, MMI32 cp, MMU32 value)
{
    CHECK(h->count < 64);
    h->entries[h->count].key = ((MMU32)kind << 24) | (MMU32)cp;
    h->entries[h->count++].value = mm_signed_word(value);
}
static void fetches(void)
{
    Host h;
    MMTextCharacterCache c;
    MMTextParagraphCache p;
    unsigned unit;
    int big, bytes, r;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
        {
            MMU32 word, mask;
            int reads;
            init(&h, unit, big);
            CHECK(mm_text_character_delta(2, &h.config, &h.input, 0, &word, &mask, &h.edit.format) == 1);
            entry(&h, 1, 2, 0xfffdc8a5UL);
            entry(&h, 1, 6, 0);
            entry(&h, 2, 3, 1);
            entry(&h, 2, 7, 0);
            entry(&h, 3, 0, 256);
            entry(&h, 3, 5, 0);
            entry(&h, 4, 1, 257);
            entry(&h, 4, 4, 0);
            memset(&c, 0, sizeof(c));
            CHECK(mm_text_fetch_character(&h.config, &c, 3, 9, &h.edit.format) == 1);
            CHECK(c.first == 3 && c.end == 4 && c.value.height == 9 && c.value.style == 0xa5 &&
                  c.value.offset == (h.config.signed_offset ? -56 : 200));
            CHECK(c.value.font == 17 && c.value.foreground == h.config.black + 256 &&
                  c.value.background == h.config.clear + 257 && h.valid == 1);
            reads = h.reads;
            h.config.height = 99;
            CHECK(mm_text_fetch_character(&h.config, &c, 3, 9, &h.edit.format) == 1 && c.value.height == 9 &&
                  h.reads == reads && h.valid == 1);
            CHECK(mm_text_fetch_character(&h.config, &c, 4, 9, &h.edit.format) == 1 && c.first == 4 && c.end == 5 &&
                  c.value.height == 96 && c.value.background == h.config.clear);
            CHECK(mm_text_fetch_character(&h.config, &c, 8, 9, &h.edit.format) == 1 && c.first == 7 && c.end == 9 &&
                  c.value.font == 11 && c.value.foreground == h.config.black);
            for (bytes = 3; bytes <= 523; bytes += bytes == 3 ? 3 : bytes == 6 ? 516 : 1)
            {
                h.length = bytes;
                memset(&c, 0, sizeof(c));
                h.warnings = 0;
                CHECK(mm_text_fetch_character(&h.config, &c, 3, 9, &h.edit.format) == 1);
                CHECK(h.warnings == (bytes < 6 || bytes > 522) && c.value.font == (h.warnings ? 11 : 17));
            }
            h.length = 6;
            h.fail = 'L';
            h.result = -2;
            h.seen = 0;
            memset(&c, 0, sizeof(c));
            r = mm_text_fetch_character(&h.config, &c, 3, 9, &h.edit.format);
            CHECK(r == -2 && c.first == 3 && c.end == 6 && c.value.font == 17 && c.value.foreground == h.config.black);
            h.fail = 0;
            h.count = 0;
            entry(&h, 128, 0, 0x0203fffbUL);
            entry(&h, 129, 3, 0xff00fff9UL);
            entry(&h, 130, 2, 0x7fff8000UL);
            memset(&p, 0, sizeof(p));
            mm_text_fetch_paragraph(&h.config, &p, 3, 9, &h.edit.format);
            CHECK(p.first == 3 && p.end == 9 && p.value.justification == 2 && p.value.indent == 3 &&
                  p.value.tab == 13 && p.value.line == 0 && p.value.extra_line == -7 && p.value.after == -32513 &&
                  p.value.extra_after == -32768);
            h.entries[0].value = 0;
            mm_text_fetch_paragraph(&h.config, &p, 3, 9, &h.edit.format);
            CHECK(p.value.justification == 2);
            p.end = 0;
            mm_text_fetch_paragraph(&h.config, &p, 3, 9, &h.edit.format);
            CHECK(!p.value.justification && p.value.tab == 18);
        }
}
static void bounds(void)
{
    Host h;
    MMI32 first, end;
    int failure;
    unsigned i;
    init(&h, 1, 0);
    first = 2;
    end = 5;
    CHECK(mm_text_paragraph_bounds(&first, &end, 9, 1, &h.edit.paragraphs) == 1 && first == 0 && end == 6 &&
          !strcmp(h.log, "ES"));
    for (i = 0; i < 2; ++i)
        for (failure = 0; failure >= -2; --failure)
        {
            init(&h, 1, 0);
            h.fail = "ES"[i];
            h.result = failure;
            first = 2;
            end = 5;
            CHECK(mm_text_paragraph_bounds(&first, &end, 9, 1, &h.edit.paragraphs) == failure && first == 2 &&
                  end == (i ? 6 : 5));
        }
    init(&h, 1, 0);
    first = 2;
    end = 5;
    CHECK(mm_text_paragraph_bounds(&first, &end, 9, 0, &h.edit.paragraphs) == 1 && first == 6 && end == 0 &&
          !strcmp(h.log, "IEINS"));
    for (i = 0; i < 5; ++i)
        for (failure = 0; failure >= -2; --failure)
        {
            init(&h, 1, 0);
            h.fail = "IEINS"[i];
            h.occurrence = i == 2 ? 2 : 1;
            h.result = failure;
            first = 2;
            end = 5;
            CHECK(mm_text_paragraph_bounds(&first, &end, 9, 0, &h.edit.paragraphs) == failure);
            CHECK(first == (i >= 2 ? 6 : 2) && end == (i >= 4 ? 6 : 5));
        }
    init(&h, 1, 0);
    h.is_start = 1;
    first = 0;
    end = 6;
    CHECK(mm_text_paragraph_bounds(&first, &end, 9, 0, &h.edit.paragraphs) == 1 && first == 0 && end == 6 &&
          !strcmp(h.log, "II"));
}
static int apply(Host *h)
{
    return mm_text_format_apply(0, &h->config, 2, 3, 9, 0, 7, &h->input, 0, &h->edit);
}
static void edits(void)
{
    Host h;
    int r, failure;
    unsigned i;
    MMTextParagraph p;
    init(&h, 1, 0);
    CHECK(apply(&h) == 1 && h.dirty && h.count == 8 && h.fixed == 0x02000001UL);
    CHECK(!strcmp(h.log, "bUFGpARkXXXXXXXXMbvV"));
    for (i = 0; i < 8; ++i)
        for (failure = -1; failure >= -2; --failure)
        {
            init(&h, 1, 0);
            h.fail = "UFGARMXV"[i];
            h.result = failure;
            r = apply(&h);
            CHECK(r == failure && !strchr(h.log, 'C'));
            if (h.fail == 'V')
                CHECK(h.dirty && h.count == 8);
            else
                CHECK(!h.dirty);
        }
    for (i = 0; i < 5; ++i)
    {
        init(&h, 1, 0);
        h.fail = "UFGAR"[i];
        h.result = 0;
        CHECK(apply(&h) == 0 && !h.dirty && !h.count);
        if (i)
            CHECK(strchr(h.log, 'C') != 0);
        else
            CHECK(!strchr(h.log, 'C'));
        if (h.fail == 'R')
            CHECK(h.free && strstr(h.log, "RfCv"));
    }
    init(&h, 1, 0);
    h.fail = 'A';
    h.result = 0;
    h.partial = 1;
    CHECK(apply(&h) == 0 && h.group == h.bytes && !h.length);
    init(&h, 1, 0);
    h.edit.read_input = read_input;
    h.edit.characters = characters;
    h.mutate_undo = 1;
    CHECK(mm_text_format_apply(0, 0, 2, 3, 9, 0, 7, 0, 0, &h.edit) == 1 && h.prepared == 1 && strstr(h.log, "UTR") &&
          strstr(h.log, "Rqk") && !strchr(h.log, 'F'));
    CHECK(h.entries[0].key == 0x01000002UL && ((MMU32)h.entries[0].value >> 16) == 19);
    init(&h, 1, 0);
    h.edit.read_input = read_input;
    h.fail = 'U';
    h.result = 0;
    CHECK(mm_text_format_apply(0, 0, 2, 3, 9, 0, 7, 0, 0, &h.edit) == 0 && !h.prepared);
    for (failure = -1; failure >= -2; --failure)
    {
        init(&h, 1, 0);
        h.reserve_false = 1;
        h.fail = 'C';
        h.result = failure;
        CHECK(apply(&h) == failure && h.free && !h.count && !h.dirty && strstr(h.log, "RfC") && !strchr(h.log, 'v'));
    }
    init(&h, 1, 0);
    CHECK(mm_text_format_apply(0, &h.config, 2, 3, 9, 0, 7, &h.input, &h.input, &h.edit) == 1);
    CHECK(!strcmp(h.log, "bURkMbvV") && h.dirty && !h.count);
    init(&h, 1, 0);
    memset(&p, 0, sizeof(p));
    p.tab = 18;
    p.line = p.after = 256;
    h.expected_first = 0;
    h.expected_end = 6;
    CHECK(mm_text_format_apply(1, &h.config, 2, 3, 9, 1, 7, &p, 0, &h.edit) == 1 && h.range_published &&
          !strncmp(h.log, "ESPbU", 5));
    init(&h, 1, 0);
    CHECK(mm_text_format_apply(1, &h.config, 2, 3, 9, 0, 7, 0, 0, &h.edit) == 1 && !strcmp(h.log, "IEINSP") &&
          h.range_first == 6 && h.range_end == 0 && !h.dirty);
    init(&h, 1, 0);
    h.fail = 'S';
    h.result = -2;
    CHECK(mm_text_format_apply(1, &h.config, 2, 3, 9, 1, 7, &p, 0, &h.edit) == -2 && !h.range_published &&
          !strchr(h.log, 'U'));
}
int main(void)
{
    deltas();
    fetches();
    bounds();
    edits();
    puts("Text format checks passed");
    return 0;
}
