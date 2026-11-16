/* Primitive cases frozen against the pre-conversion native implementation. */
#ifndef STRING_OPS_CASES_H
#define STRING_OPS_CASES_H
#include "mm/types.h"
#include <string.h>
typedef struct StringOpsCases {
    void *context;
    void (*word)(void *, MMU32);
    int (*valid)(void *, unsigned char *, int terminated);
    MMU32 (*length)(void *, unsigned char *);
    int (*equal)(void *, unsigned char *, MMI32, unsigned char *, MMI32);
    MMU32 (*compare)(void *, unsigned char *, MMI32, unsigned char *, MMI32);
    MMU32 (*flags)(void *, unsigned);
} StringOpsCases;
static void string_ops_cases(const StringOpsCases *api)
{
    unsigned char a[513], b[513], counted[257];
    MMI32 lengths[] = {0, 1, 2, 7, 254, 255, 256, 510, 511};
    unsigned i, j, li, ri;
    MMI32 count, position;
    void *context = api->context;
    for (i = 0; i < 256; ++i) {
        api->word(context, api->flags(context, i));
        for (j = 0; j < 256; ++j) {
            a[0] = (unsigned char)i; b[0] = (unsigned char)j;
            api->word(context, api->equal(context, a, 1, b, 1));
            api->word(context, api->compare(context, a, 1, b, 1));
        }
    }
    for (i = 0; i < 512; ++i) a[i] = b[i] = (unsigned char)(1 + i % 255);
    a[512] = b[512] = 0;
    for (li = 0; li < sizeof(lengths) / sizeof(lengths[0]); ++li) {
        for (ri = 0; ri < sizeof(lengths) / sizeof(lengths[0]); ++ri) {
            api->word(context, api->equal(context, a, lengths[li], b, lengths[ri]));
            api->word(context, api->compare(context, a, lengths[li], b, lengths[ri]));
        }
    }
    for (position = 0; position <= 512; ++position) {
        a[position] = 0;
        api->word(context, api->length(context, a));
        a[position] = (unsigned char)(1 + position % 255);
    }
    for (count = 0; count <= 255; ++count) {
        counted[0] = (unsigned char)count;
        memset(counted + 1, 'A', (size_t)count); counted[count + 1] = 0;
        api->word(context, api->valid(context, counted, 0));
        api->word(context, api->valid(context, counted, 1));
        counted[count + 1] = 1;
        api->word(context, api->valid(context, counted, 1));
        counted[count + 1] = 0;
        for (position = 1; position <= count; ++position) {
            counted[position] = 0;
            api->word(context, api->valid(context, counted, 0));
            api->word(context, api->valid(context, counted, 1));
            counted[position] = 'A';
        }
    }
}
#endif
