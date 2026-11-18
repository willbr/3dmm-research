/* Licensed under the MIT License. */
#include "mm/vmm_dependencies.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct Fixture
{
    MMVmmPackageIdentity catalog[3], embedded[4];
    size_t catalog_count, kept_count;
    const char *kept[4], *pending;
    size_t selections[4];
    int available, writable, opened, selected, checked, externals, inspected;
} Fixture;

static int library_available(void *context, size_t index)
{
    Fixture *f = (Fixture *)context;
    assert(index == 1);
    ++f->checked;
    return f->available;
}

static int external(void *context, size_t index)
{
    Fixture *f = (Fixture *)context;
    assert(index == 1 && f->checked == 1);
    ++f->externals;
    return 1;
}

static int open_catalog(void *context)
{
    Fixture *f = (Fixture *)context;
    assert(!f->opened && (!f->writable || f->checked));
    f->opened = 1;
    return 1;
}

static int installed(void *context, size_t slot, MMVmmPackageIdentity *identity)
{
    Fixture *f = (Fixture *)context;
    if (slot >= f->catalog_count) return 0;
    *identity = f->catalog[slot];
    return 1;
}

static int embedded(void *context, size_t index, MMVmmPackageIdentity *identity)
{
    Fixture *f = (Fixture *)context;
    assert(f->opened && (index == 0 || index == 2));
    ++f->inspected;
    *identity = f->embedded[index];
    return 1;
}

static int select_package(void *context, size_t index, size_t slot, const char **name)
{
    Fixture *f = (Fixture *)context;
    f->selections[f->selected++] = slot;
    if (slot == MM_VMM_NO_CATALOG_ENTRY)
    {
        f->pending = f->embedded[index].name;
        if (f->writable) f->catalog[f->catalog_count++] = f->embedded[index];
    }
    else f->pending = f->catalog[slot].name;
    *name = f->pending;
    return 1;
}

static const char *kept_name(void *context, size_t index)
{
    return ((Fixture *)context)->kept[index];
}

static int keep(void *context)
{
    Fixture *f = (Fixture *)context;
    f->kept[f->kept_count++] = f->pending;
    return 1;
}

static const MMVmmEntry dependencies[] = {
    {"Stage", 5, 1, {0, 0}}, {"Stock", 5, 0, {0, 0}},
    {"sTAGE", 5, 1, {0, 0}}, {"Other", 5, 1, {0, 0}}
};
static const Fixture initial = {
    {{"Stage", "First"}, {"Other", "Old"}},
    {{"stage", "FIRST"}, {NULL, NULL}, {"STAGE", "Second"}, {NULL, NULL}},
    2, 0, {NULL}, NULL, {0}, 1, 0, 0, 0, 0, 0, 0
};

int main(void)
{
    Fixture f;
    size_t failed;
    MMVmmDependencyServices io = {NULL, library_available, external, open_catalog, installed,
        embedded, select_package, kept_name, keep};
    io.context = &f;
    f = initial;
    assert(mm_vmm_resolve_dependencies(dependencies, 4, MM_VMM_PRIVATE_CONTENT, &io, &failed) == MM_VMM_DEPENDENCIES_OK);
    assert(failed == 4 && f.checked == 1 && f.externals == 1 && f.inspected == 2);
    assert(f.kept_count == 3 && f.selections[0] == 0 && f.selections[1] == MM_VMM_NO_CATALOG_ENTRY && f.selections[2] == 1);
    assert(!strcmp(f.kept[0], "Stage") && !strcmp(f.kept[1], "STAGE") && !strcmp(f.kept[2], "Other"));

    f = initial;
    f.writable = 1;
    f.available = 0;
    assert(mm_vmm_resolve_dependencies(dependencies, 4, MM_VMM_WRITABLE_CATALOG, &io, &failed) == MM_VMM_LIBRARY_MISSING);
    assert(failed == 1 && !f.opened && !f.selected && !f.inspected && !f.externals);

    f = initial;
    f.writable = 1;
    f.catalog[0] = f.catalog[1];
    f.catalog_count = 1;
    assert(mm_vmm_resolve_dependencies(dependencies, 4, MM_VMM_WRITABLE_CATALOG, &io, &failed) == MM_VMM_DEPENDENCIES_OK);
    assert(f.inspected == 1 && f.checked == 1 && f.externals == 1 && f.selected == 3);
    assert(f.kept_count == 2 && f.selections[0] == MM_VMM_NO_CATALOG_ENTRY && f.selections[1] == 1 && f.selections[2] == 0);
    assert(!strcmp(f.kept[0], "stage") && !strcmp(f.kept[1], "Other"));

    f = initial;
    f.embedded[2].name = "Wrong package";
    assert(mm_vmm_resolve_dependencies(dependencies, 4, MM_VMM_PRIVATE_CONTENT, &io, &failed) == MM_VMM_PACKAGE_NAME_MISMATCH);
    assert(failed == 2 && f.selected == 1 && f.inspected == 2);
    puts("VMM dependency order, installed reuse, shortnames and external preflight passed.");
    return 0;
}
