#include "mm/resource_lookup.h"

int mm_resource_lookup(const MMResourceLookup *lookup, void **out)
{
    size_t cursor = 0;
    void *candidate, *result;
    int status;
    while ((status = lookup->next(lookup->context, &cursor, &candidate)) == MM_RESOURCE_LOOKUP_OK)
    {
        status = lookup->probe(lookup->context, candidate, &result);
        if (status == MM_RESOURCE_LOOKUP_OK)
            *out = result;
        if (status != MM_RESOURCE_LOOKUP_MISSING)
            return status;
    }
    return status;
}
