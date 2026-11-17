/* File factories, registries and name transactions converted from Kauai
 * file.cpp/file_native.cpp. Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License. */
#ifndef MM_ENGINE_FILE_OPS_H
#define MM_ENGINE_FILE_OPS_H
#include "file_owner.h"
#include "reference.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MM_FILE_NAME_MISSING 0
enum { MM_FILE_OP_OPEN = 1, MM_FILE_OP_CREATE, MM_FILE_OP_SWAP, MM_FILE_OP_RENAME };
enum { MM_FILE_OP_TEMP_OPEN = 1, MM_FILE_OP_CREATE_OPEN, MM_FILE_OP_SWAP_SELF,
       MM_FILE_OP_SWAP_DIRECTORY, MM_FILE_OP_RENAME_DIRECTORY, MM_FILE_OP_RECOVERY };
typedef struct MMFileOpsHost {
    void *context;
    void (*enter_list)(void *);
    void (*leave_list)(void *);
    void *(*first)(void *);
    void *(*next)(void *, void *object);
    void (*enter_object)(void *, void *object);
    void (*leave_object)(void *, void *object);
    void *(*name)(void *, void *object);
    MMFileOwner *(*owner)(void *, void *object);
    MMReference *(*references)(void *, void *object);
    void (*owner_host)(void *, void *object, MMFileOwnerHost *);
    int (*equal_names)(void *, void *a, void *b);
    void (*copy_name)(void *, void *destination, const void *source);
    int (*unique_name)(void *, void *name);
    int (*temp_name)(void *, void *name);
    int (*same_directory)(void *, void *a, void *b);
    int (*rename_name)(void *, void *source, void *destination);
    int (*name_status)(void *, void *name);
    int (*delete_name)(void *, void *name);
    void *(*construct)(void *, void *name, MMU32 flags);
    void (*retain)(void *, void *object);
    void (*destroy)(void *, void *object);
    int (*swap_objects)(void *, void *a, void *b);
    int (*rename_object)(void *, void *object, void *name);
    void (*validate_name)(void *, void *name);
    void (*validate_object)(void *, void *object);
    void (*issue)(void *, int code);
    void (*assertion)(void *, int code);
} MMFileOpsHost;
/* Explicit provider context; no global registry in C. Names and objects are
 * opaque borrowed tokens. All primitive services are required except validation,
 * assertions and issue reporting. Locks are recursive; two-object transactions
 * enter a then b and leave a then b, retaining native lock order. Callers must
 * serialize competing operations on the same objects/registry. Lookup borrows a
 * result without retaining; cached open retains once; fresh construct owns one
 * reference and registers the object. Destroy closes/detaches and invalidates
 * that owner; close_unmarked saves next before calling it. Other callbacks cannot
 * delete/move owners, mutate links or unwind. Copy/construct/rename services may
 * perform only their designated publication. Owner hosts follow file_owner.h.
 * Scratch name storage must be live, initialized and distinct from owner/input
 * names. Temp, rename, swap and set_name adapters validate their arguments before
 * constructing native scratch objects; open/create/find perform name validation.
 * Directory assertions are advisory and skipped without an assertion hook.
 * Rename primitives leave name records unchanged; copy publishes logical names.
 * Failed recovery publishes the known physical location before reopening. Swap
 * deliberately ignores reopen results, while rename fails on reopen failure.
 * Nonmissing/unknown destination status triggers an attempted deletion whose
 * result is ignored before rename. Registry clear_marks retains sticky marks.
 * Returned files need the caller's normal release; final C++ facade removal and
 * native name/physical-service implementations are separate integration work. */
void *mm_file_ops_find(const MMFileOpsHost *, void *name);
void *mm_file_ops_open(const MMFileOpsHost *, void *name, MMU32 flags);
void *mm_file_ops_create(const MMFileOpsHost *, void *name, MMU32 flags);
void *mm_file_ops_temp(const MMFileOpsHost *, void *basis, void *scratch);
int mm_file_ops_swap(const MMFileOpsHost *, void *a, void *b, void *scratch);
int mm_file_ops_rename(const MMFileOpsHost *, void *object, void *name);
int mm_file_ops_set_name(const MMFileOpsHost *, void *object, void *name);
void mm_file_ops_clear_marks(const MMFileOpsHost *);
void mm_file_ops_close_unmarked(const MMFileOpsHost *);
void mm_file_ops_shutdown(const MMFileOpsHost *);
int mm_file_ops_contains(const MMFileOpsHost *, void *object);
#ifdef __cplusplus
}
#endif
#endif
