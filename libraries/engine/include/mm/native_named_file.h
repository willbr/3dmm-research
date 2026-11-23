#ifndef MM_NATIVE_NAMED_FILE_H
#define MM_NATIVE_NAMED_FILE_H

#include "file_owner.h"
#include "native_files.h"
#include "native_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMNativeNamedFileDiagnostics
{
    void (*issue)(void *, int code);
    void (*validate)(void *, int base_only);
    void (*arguments)(void *, int operation, const void *, MMI32 bytes, MMI32 position);
    void (*assertion)(void *, int code);
    int debug_read;
} MMNativeNamedFileDiagnostics;

typedef struct MMNativeNamedFile
{
    MMFileOwner state;
    MMNativeMutex *mutex;
    const MMNativePathChar *path;
    const MMNativeNamedFileDiagnostics *diagnostics;
    void *diagnostic_context;
} MMNativeNamedFile;

/* The owner keeps its handle and recursive lock. The path is a borrowed
 * terminated buffer at a stable address. Name transactions can update its
 * contents with the lock held. Diagnostic services are optional and borrowed.
 * Close with mm_file_owner_close before disposing the lock. */
int mm_native_named_file_init(MMNativeNamedFile *, const MMNativePathChar *, MMU32 flags,
                              const MMNativeNamedFileDiagnostics *, void *diagnostic_context, int *error);
void mm_native_named_file_dispose(MMNativeNamedFile *);
void mm_native_named_file_enter(MMNativeNamedFile *);
void mm_native_named_file_leave(MMNativeNamedFile *);
MMFileOwnerHost mm_native_named_file_host(MMNativeNamedFile *);
MMReadStream mm_native_named_file_stream(MMNativeNamedFile *);

#ifdef __cplusplus
}
#endif
#endif
