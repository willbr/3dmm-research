#ifndef MM_ENGINE_RESOURCE_LOOKUP_H
#define MM_ENGINE_RESOURCE_LOOKUP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MM_RESOURCE_LOOKUP_OK = 0, MM_RESOURCE_LOOKUP_MISSING = -1 };

typedef struct MMResourceLookup
{
    void *context;
    int (*next)(void *, size_t *cursor, void **candidate);
    int (*probe)(void *, void *candidate, void **result);
} MMResourceLookup;

/* Search in provider order. next starts with a zero cursor and returns MISSING
 * when exhausted. A probe's MISSING advances to the next candidate. Other errors
 * stop immediately. The result is untouched except on OK. */
int mm_resource_lookup(const MMResourceLookup *, void **result);

#ifdef __cplusplus
}
#endif
#endif
