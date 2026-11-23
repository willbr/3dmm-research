/* File factories, registries and name transactions converted from Kauai
 * file.cpp/file_native.cpp. Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License. */
#ifndef MM_ENGINE_FILE_OPS_H
#define MM_ENGINE_FILE_OPS_H
#include "file_owner.h"
#include "linked_list.h"
#include "reference.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MM_FILE_NAME_MISSING 0
enum { MM_FILE_OP_OPEN = 1, MM_FILE_OP_CREATE, MM_FILE_OP_SWAP, MM_FILE_OP_RENAME };
enum { MM_FILE_OP_TEMP_OPEN = 1, MM_FILE_OP_CREATE_OPEN, MM_FILE_OP_SWAP_SELF,
       MM_FILE_OP_SWAP_DIRECTORY, MM_FILE_OP_RENAME_DIRECTORY, MM_FILE_OP_RECOVERY };

typedef struct MMRegisteredFile {
    MMLinkedListNode link;
    void *object;
    void *name;
    MMFileOwner *owner;
    MMReference *references;
    void *file_context;
    MMFileOwnerHost (*file_host)(void *);
} MMRegisteredFile;

typedef struct MMFileRegistry {
    MMLinkedListNode *head;
    void *lock;
    void (*enter)(void *);
    void (*leave)(void *);
} MMFileRegistry;

/* The registry owns the list. File records and their name, owner and
 * reference storage stay at stable addresses until detached. Detach a file
 * from its registry before moving it to another. Locks are recursive, or may
 * both be NULL when callers supply synchronization. First/next borrow a
 * record. The caller serializes external iteration. */
void mm_file_registry_init(MMFileRegistry *, void *lock, void (*enter)(void *), void (*leave)(void *));
void mm_registered_file_init(MMRegisteredFile *, void *object, void *name, MMFileOwner *, MMReference *,
                             void *file_context, MMFileOwnerHost (*file_host)(void *));
void mm_file_registry_attach(MMFileRegistry *, MMRegisteredFile *);
void mm_file_registry_detach(MMFileRegistry *, MMRegisteredFile *);
MMRegisteredFile *mm_file_registry_first(const MMFileRegistry *);
MMRegisteredFile *mm_file_registry_next(const MMRegisteredFile *);
int mm_file_registry_contains(const MMFileRegistry *, const MMRegisteredFile *);

typedef struct MMFileOpsHost {
    MMFileRegistry *registry;
    void *context;
    int (*equal_names)(void *, void *a, void *b);
    void (*copy_name)(void *, void *destination, const void *source);
    int (*unique_name)(void *, void *name);
    int (*temp_name)(void *, void *name);
    int (*same_directory)(void *, void *a, void *b);
    int (*rename_name)(void *, void *source, void *destination);
    int (*name_status)(void *, void *name);
    int (*delete_name)(void *, void *name);
    MMRegisteredFile *(*construct)(void *, void *name, MMU32 flags);
    void (*retain)(void *, void *object);
    void (*destroy)(void *, void *object);
    void (*validate_name)(void *, void *name);
    void (*validate_object)(void *, void *object);
    void (*issue)(void *, int code);
    void (*assertion)(void *, int code);
} MMFileOpsHost;
/* The caller supplies a registry. Names and objects are opaque borrowed tokens.
 * Operations require the primitive services they use. Validation, assertions
 * and issue reporting are optional. File owner locks are recursive. Two-object transactions
 * enter a then b and leave a then b, retaining native lock order. Callers must
 * serialize competing operations on the same objects/registry. Lookup borrows a
 * result without retaining; cached open retains once; fresh construct owns one
 * reference and registers the object. Destroy closes/detaches and invalidates
 * that owner; close_unmarked saves next before calling it. Other callbacks cannot
 * delete/move owners, mutate links or unwind. Copy/construct/rename services may
 * perform only their designated publication. Owner hosts follow file_owner.h.
 * Scratch name storage must be live, initialized and distinct from owner/input
 * names. Factories and transactions validate through the host hooks. Name
 * comparison alone uses the file lock and assumes valid arguments.
 * Directory assertions are advisory and skipped without an assertion hook.
 * Rename primitives leave name records unchanged; copy publishes logical names.
 * Failed recovery publishes the known physical location before reopening. Swap
 * deliberately ignores reopen results, while rename fails on reopen failure.
 * Nonmissing/unknown destination status triggers an attempted deletion whose
 * result is ignored before rename. Registry clear_marks retains sticky marks.
 * Returned files need the caller's normal release. */
MMRegisteredFile *mm_file_ops_find(const MMFileOpsHost *, void *name);
MMRegisteredFile *mm_file_ops_open(const MMFileOpsHost *, void *name, MMU32 flags);
MMRegisteredFile *mm_file_ops_create(const MMFileOpsHost *, void *name, MMU32 flags);
/* Choose an unused name in the basis directory or the default temp directory. */
int mm_file_ops_temp_name(const MMFileOpsHost *, void *basis, void *scratch);
MMRegisteredFile *mm_file_ops_temp(const MMFileOpsHost *, void *basis, void *scratch);
int mm_registered_file_set_flags(const MMRegisteredFile *, MMU32 flags, MMU32 mask);
int mm_file_ops_matches(const MMFileOpsHost *, const MMRegisteredFile *, void *name);
int mm_file_ops_swap(const MMFileOpsHost *, MMRegisteredFile *a, MMRegisteredFile *b, void *scratch);
int mm_file_ops_rename(const MMFileOpsHost *, MMRegisteredFile *, void *name);
/* Publish by attempting to delete the destination before renaming. Uses
 * filesystem status even if a caller holds another registered file. */
int mm_file_ops_rename_replacing(const MMFileOpsHost *, MMRegisteredFile *, void *name);
int mm_file_ops_set_name(const MMFileOpsHost *, MMRegisteredFile *, void *name, void *scratch);
void mm_file_ops_clear_marks(const MMFileOpsHost *);
void mm_file_ops_close_unmarked(const MMFileOpsHost *);
void mm_file_ops_shutdown(const MMFileOpsHost *);
#ifdef __cplusplus
}
#endif
#endif
