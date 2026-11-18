/* Licensed under the MIT License. */
#include "mm/vmm_dependencies.h"
#include "mm/vxp_package.h"
#include <stdlib.h>

typedef struct NamedSlot
{
    const char *name;
    size_t index;
} NamedSlot;

static int order_names(const void *a, const void *b)
{
    return mm_vxp_compare_names(((const NamedSlot *)a)->name, ((const NamedSlot *)b)->name);
}

static unsigned char *repeated_names(const MMVmmEntry *entries, size_t count)
{
    unsigned char *repeated;
    NamedSlot *names;
    size_t i, next = 0;
    if (count > (size_t)-1 / sizeof(*names)) return NULL;
    repeated = (unsigned char *)calloc(count, 1);
    names = (NamedSlot *)malloc(count * sizeof(*names));
    if (!repeated || !names) { free(repeated); free(names); return NULL; }
    for (i = 0; i < count; ++i)
        if (entries[i].archive_length)
        {
            names[next].name = entries[i].name;
            names[next++].index = i;
        }
    qsort(names, next, sizeof(*names), order_names);
    for (i = 1; i < next; ++i)
        if (!mm_vxp_compare_names(names[i - 1].name, names[i].name))
            repeated[names[i - 1].index] = repeated[names[i].index] = 1;
    free(names);
    return repeated;
}

static MMVmmDependencyResult inspect(const MMVmmDependencyServices *services, size_t index,
                                     const char *declared, MMVmmPackageIdentity *identity)
{
    if (!services->embedded(services->context, index, identity)) return MM_VMM_DEPENDENCY_IO;
    if (mm_vxp_compare_names(declared, identity->name)) return MM_VMM_PACKAGE_NAME_MISMATCH;
    return MM_VMM_DEPENDENCIES_OK;
}

static MMVmmDependencyResult resolve_package(const MMVmmDependencyServices *services, size_t index,
                                            const char *name, int repeated, const char **resolved_name)
{
    void *context = services->context;
    MMVmmPackageIdentity embedded, installed;
    MMVmmDependencyResult status;
    size_t slot;
    if (repeated)
    {
        status = inspect(services, index, name, &embedded);
        if (status != MM_VMM_DEPENDENCIES_OK) return status;
    }
    for (slot = 0; services->installed(context, slot, &installed); ++slot)
        if (!mm_vxp_compare_names(name, installed.name) &&
            (!repeated || !mm_vxp_compare_names(embedded.short_name, installed.short_name)))
            return services->select(context, index, slot, resolved_name)
                ? MM_VMM_DEPENDENCIES_OK : MM_VMM_DEPENDENCY_IO;
    if (!repeated)
    {
        status = inspect(services, index, name, &embedded);
        if (status != MM_VMM_DEPENDENCIES_OK) return status;
    }
    return services->select(context, index, MM_VMM_NO_CATALOG_ENTRY, resolved_name)
        ? MM_VMM_DEPENDENCIES_OK : MM_VMM_DEPENDENCY_IO;
}

static int seen_name(const MMVmmDependencyServices *services, const char *name, size_t kept)
{
    size_t i;
    for (i = 0; i < kept; ++i)
        if (!mm_vxp_compare_names(name, services->kept_name(services->context, i))) return 1;
    return 0;
}

MMVmmDependencyResult mm_vmm_resolve_dependencies(const MMVmmEntry *entries, size_t count, MMVmmCatalogMode mode,
                                                   const MMVmmDependencyServices *services, size_t *failed)
{
    void *context = services->context;
    unsigned char *repeated = NULL;
    size_t i, kept = 0;
    int catalog_open = 0, writable = mode == MM_VMM_WRITABLE_CATALOG;
    const char *resolved_name;
    MMVmmDependencyResult status = MM_VMM_DEPENDENCIES_OK;
    *failed = count;
    if (writable)
    {
        for (i = 0; i < count; ++i)
            if (!entries[i].archive_length && !services->library_available(context, i))
            {
                *failed = i;
                return MM_VMM_LIBRARY_MISSING;
            }
    }
    else if (count)
    {
        repeated = repeated_names(entries, count);
        if (!repeated) return MM_VMM_DEPENDENCIES_MEMORY;
    }

    for (i = 0; i < count; ++i)
    {
        if (!entries[i].archive_length)
        {
            if (!writable && !services->library_available(context, i)) { status = MM_VMM_LIBRARY_MISSING; break; }
            if (!services->external(context, i)) { status = MM_VMM_DEPENDENCY_IO; break; }
            continue;
        }
        if (!catalog_open)
        {
            if (!services->open_catalog(context)) { status = MM_VMM_DEPENDENCY_IO; break; }
            catalog_open = 1;
        }
        status = resolve_package(services, i, entries[i].name, repeated && repeated[i], &resolved_name);
        if (status != MM_VMM_DEPENDENCIES_OK) break;
        if (writable && seen_name(services, resolved_name, kept)) continue;
        if (!services->keep(context)) { status = MM_VMM_DEPENDENCY_IO; break; }
        ++kept;
    }
    free(repeated);
    *failed = i;
    return status;
}
