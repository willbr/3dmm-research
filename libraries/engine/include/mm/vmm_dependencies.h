/* Licensed under the MIT License. */
#ifndef MM_ENGINE_VMM_DEPENDENCIES_H
#define MM_ENGINE_VMM_DEPENDENCIES_H
#include "vmm_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MMVmmCatalogMode
{
    /* Verify external libraries before imports. Keep the first active display name. */
    MM_VMM_WRITABLE_CATALOG,
    /* For repeated declarations, validate each CFG and match its shortname too. */
    MM_VMM_PRIVATE_CONTENT
} MMVmmCatalogMode;

typedef enum MMVmmDependencyResult
{
    MM_VMM_DEPENDENCIES_OK,
    MM_VMM_DEPENDENCIES_MEMORY,
    MM_VMM_LIBRARY_MISSING,
    MM_VMM_PACKAGE_NAME_MISMATCH,
    MM_VMM_DEPENDENCY_IO
} MMVmmDependencyResult;

typedef struct MMVmmPackageIdentity
{
    const char *name, *short_name;
} MMVmmPackageIdentity;

#define MM_VMM_NO_CATALOG_ENTRY ((size_t)-1)

typedef struct MMVmmDependencyServices
{
    void *context;
    /* Callbacks use dependency indices in the original VMM order. Return
     * nonzero on success except installed, where zero ends enumeration. */
    int (*library_available)(void *, size_t dependency);
    int (*external)(void *, size_t dependency);
    int (*open_catalog)(void *);
    int (*installed)(void *, size_t slot, MMVmmPackageIdentity *);
    int (*embedded)(void *, size_t dependency, MMVmmPackageIdentity *);
    /* On an installed match the slot is valid until select returns. With no
     * match, embedded has already supplied and validated this row's metadata.
     * The returned name is borrowed through the subsequent keep call. */
    int (*select)(void *, size_t dependency, size_t slot, const char **resolved_name);
    /* Previously kept names, from index zero. A new resolution starts empty. */
    const char *(*kept_name)(void *, size_t index);
    int (*keep)(void *);
} MMVmmDependencyServices;

/* Builds an ordered selection using host-owned catalog and staging services.
 * Installed and embedded identities are borrowed until the next operation
 * that can change their respective providers. Keep the selection private
 * until success. Imports performed by a writable catalog remain installed.
 * On a row failure failed receives its original index, otherwise count. */
MMVmmDependencyResult mm_vmm_resolve_dependencies(const MMVmmEntry *, size_t count, MMVmmCatalogMode,
                                                   const MMVmmDependencyServices *, size_t *failed);

typedef enum MMVmmLibrary
{
    MM_VMM_BASE_LIBRARY,
    MM_VMM_NICKELODEON,
    MM_VMM_DORAEMON,
    MM_VMM_EXPANSION_PACK,
    MM_VMM_LIBRARY_COUNT,
    MM_VMM_UNKNOWN_LIBRARY = -1
} MMVmmLibrary;

const char *mm_vmm_library_name(MMVmmLibrary);
MMVmmLibrary mm_vmm_library_id(const char *);

#define MM_VMM_SAVE_BASE ((size_t)-1)
#define MM_VMM_SAVE_STORYBOARD ((size_t)-2)

/* Output storage holds count + 1 slots. The published count is unchanged on
 * failure. An active-catalog save emits the base dependency first, then the
 * first occurrence of each display name in input order. Pass libraries before
 * packages. Embedded archive I/O can be deferred until after this selection. */
MMVmmDependencyResult mm_vmm_save_active_dependencies(const MMVmmEntry *, size_t count,
                                                       size_t *slots, size_t *selected);

/* Copy authored declarations in order, except old storyboard packages. The
 * callback inspects only embedded entries and returns nonzero on success.
 * Append the generated replacement at the MM_VMM_SAVE_STORYBOARD slot. */
typedef int (*MMVmmStoryboardPackage)(void *, size_t slot, int *storyboard);
MMVmmDependencyResult mm_vmm_save_storyboard_dependencies(const MMVmmEntry *, size_t count,
                                                           MMVmmStoryboardPackage, void *,
                                                           size_t *slots, size_t *selected);

#ifdef __cplusplus
}
#endif
#endif
