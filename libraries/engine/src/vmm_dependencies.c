/* Licensed under the MIT License. */
#include "mm/vmm_dependencies.h"
#include "mm/vxp_package.h"
#include <stdlib.h>

typedef struct NamedSlot
{
    const MMVmmEntry *entry;
    size_t index;
} NamedSlot;

static int compare_entries(const MMVmmEntry *a, const MMVmmEntry *b)
{
    int order = mm_vxp_compare_names(a->name, b->name);
    if (order) return order;
    return (a->name_length > b->name_length) - (a->name_length < b->name_length);
}

static int order_names(const void *a, const void *b)
{
    const NamedSlot *left = (const NamedSlot *)a;
    const NamedSlot *right = (const NamedSlot *)b;
    int order = compare_entries(left->entry, right->entry);
    if (order) return order;
    return (left->index > right->index) - (left->index < right->index);
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
            names[next].entry = &entries[i];
            names[next++].index = i;
        }
    qsort(names, next, sizeof(*names), order_names);
    for (i = 1; i < next; ++i)
        if (!compare_entries(names[i - 1].entry, names[i].entry))
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

static const char *const library_names[] = {"3dmm", "Nickelodeon", "Doraemon", "3DMM Expansion Pack"};

const char *mm_vmm_library_name(MMVmmLibrary library)
{
    return library >= MM_VMM_BASE_LIBRARY && library < MM_VMM_LIBRARY_COUNT ? library_names[library] : NULL;
}

MMVmmLibrary mm_vmm_library_id(const char *name)
{
    int i;
    for (i = 0; i < MM_VMM_LIBRARY_COUNT; ++i)
        if (!mm_vxp_compare_names(name, library_names[i])) return (MMVmmLibrary)i;
    return MM_VMM_UNKNOWN_LIBRARY;
}

static int order_slots(const void *a, const void *b)
{
    size_t left = *(const size_t *)a, right = *(const size_t *)b;
    return (left > right) - (left < right);
}

MMVmmDependencyResult mm_vmm_save_active_dependencies(const MMVmmEntry *entries, size_t count,
                                                       size_t *slots, size_t *selected)
{
    NamedSlot *rows;
    size_t i, used = 0, output = 1;
    slots[0] = MM_VMM_SAVE_BASE;
    if (count)
    {
        if (count > (size_t)-1 / sizeof(*rows)) return MM_VMM_DEPENDENCIES_MEMORY;
        rows = (NamedSlot *)malloc(count * sizeof(*rows));
        if (!rows) return MM_VMM_DEPENDENCIES_MEMORY;
        for (i = 0; i < count; ++i)
        {
            if (entries[i].name_length == 4 && !mm_vxp_compare_names(entries[i].name, library_names[MM_VMM_BASE_LIBRARY]))
                continue;
            rows[used].entry = &entries[i];
            rows[used++].index = i;
        }
        qsort(rows, used, sizeof(*rows), order_names);
        for (i = 0; i < used; ++i)
            if (i == 0 || compare_entries(rows[i - 1].entry, rows[i].entry))
                slots[output++] = rows[i].index;
        free(rows);
        if (output > 2) qsort(slots + 1, output - 1, sizeof(*slots), order_slots);
    }
    *selected = output;
    return MM_VMM_DEPENDENCIES_OK;
}

MMVmmDependencyResult mm_vmm_save_storyboard_dependencies(const MMVmmEntry *entries, size_t count,
                                                           MMVmmStoryboardPackage inspect, void *context,
                                                           size_t *slots, size_t *selected)
{
    size_t i, output = 0;
    int storyboard;
    for (i = 0; i < count; ++i)
    {
        storyboard = 0;
        if (entries[i].archive_length && !inspect(context, i, &storyboard)) return MM_VMM_DEPENDENCY_IO;
        if (!storyboard) slots[output++] = i;
    }
    slots[output++] = MM_VMM_SAVE_STORYBOARD;
    *selected = output;
    return MM_VMM_DEPENDENCIES_OK;
}
