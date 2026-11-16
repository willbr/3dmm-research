/* Format cases shared by the original native and converted C fixtures. */
#ifndef STRING_FORMAT_CASES_H
#define STRING_FORMAT_CASES_H
#include "mm/types.h"
#include <stdio.h>
#include <string.h>
typedef struct FormatCase {
    const char *pattern;
    unsigned control;
    MMU32 number;
    const char *text;
    MMI32 count;
    int invalid;
} FormatCase;
typedef void (*FormatCaseRun)(void *, const FormatCase *);
static void format_cases(void *context, FormatCaseRun run)
{
    const char *flags[] = {"", "-", "+", "0", "-0", "0-", "+0", "0+", "--++00", "+-0", "0-+"};
    MMI32 widths[] = {0, 1, 2, 4, 7, 8, 9, 10, 32, 127, 254, 255, 256, 10000};
    MMI32 prefixes[] = {0, 1, 17, 220, 240};
    MMI32 lengths[] = {0, 1, 3, 127, 254, 255, 300};
    MMU32 values[] = {0, 1, 9, 10, 0x7fffffffUL, 0x80000000UL, 0xffffffffUL, 0x01000200UL};
    const char *invalid[] = {"%", "A%", "AB%q", "ABC%<0>d", "A%--", "AB%0", "ABC%12", "%>d", "%<d", "X%2q"};
    char pattern[256], text[301];
    FormatCase c;
    unsigned fi, wi, pi, ci, vi, ti;
    MMI32 i, prefix;
    memset(&c, 0, sizeof(c));
    c.pattern = pattern; c.text = text;
    for (pi = 0; pi < sizeof(prefixes) / sizeof(prefixes[0]); ++pi) {
        prefix = prefixes[pi];
        memset(pattern, 'P', (size_t)prefix);
        for (fi = 0; fi < sizeof(flags) / sizeof(flags[0]); ++fi) {
            for (wi = 0; wi < sizeof(widths) / sizeof(widths[0]); ++wi) {
                for (ci = 0; ci < 7; ++ci) {
                    c.control = (unsigned)"duxfczs"[ci];
                    sprintf(pattern + prefix, "%%%s%ld%c!", flags[fi], (long)widths[wi], (int)c.control);
                    if (c.control == 'z' || c.control == 's') {
                        for (ti = 0; ti < sizeof(lengths) / sizeof(lengths[0]); ++ti) {
                            c.count = lengths[ti];
                            if (c.control == 'z' && c.count > 255) continue;
                            for (i = 0; i < c.count; ++i) text[i] = (char)('A' + i % 26);
                            text[c.count] = 0;
                            run(context, &c);
                        }
                    } else {
                        for (vi = 0; vi < sizeof(values) / sizeof(values[0]); ++vi) {
                            c.number = values[vi];
                            if (c.control == 'c') c.number = c.number % 255 + 1;
                            run(context, &c);
                        }
                    }
                }
            }
        }
    }
    /* Every literal/field capacity boundary, including literal-only full output. */
    for (prefix = 0; prefix <= 253; ++prefix) {
        memset(pattern, 'P', (size_t)prefix);
        for (ci = 0; ci < 3; ++ci) {
            c.control = (unsigned)"cu%"[ci];
            sprintf(pattern + prefix, "%%%c", (int)c.control);
            for (vi = 0; vi < 2; ++vi) { c.number = vi ? 10 : 65; run(context, &c); }
        }
    }
    c.control = 0;
    for (prefix = 0; prefix <= 255; ++prefix) {
        memset(pattern, 'L', (size_t)prefix); pattern[prefix] = 0;
        run(context, &c);
    }
    for (ci = 0; ci < sizeof(invalid) / sizeof(invalid[0]); ++ci) {
        c.pattern = invalid[ci]; c.invalid = 1; run(context, &c);
    }
}
#endif
