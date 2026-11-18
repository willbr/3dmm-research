/* Licensed under the MIT License. */
#ifndef MM_ENGINE_VMM_WIRE_H
#define MM_ENGINE_VMM_WIRE_H
#include "wide.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MMVmmResult
{
    MM_VMM_OK,
    MM_VMM_SHORT_READ,
    MM_VMM_READ_FAILED,
    MM_VMM_SIGNATURE,
    MM_VMM_DEPENDENCY_COUNT,
    MM_VMM_EMPTY_NAME,
    MM_VMM_INVALID_NAME,
    MM_VMM_START_MARKER,
    MM_VMM_SHORT_ARCHIVE,
    MM_VMM_END_MARKER,
    MM_VMM_MOVIE_MAGIC,
    MM_VMM_NO_MEMORY,
    MM_VMM_OUTPUT_COUNT,
    MM_VMM_OUTPUT_ENTRY,
    MM_VMM_WRITE_FAILED
} MMVmmResult;

enum { MM_VMM_OUTPUT_ENTRY_LIMIT = 10000 };

typedef struct MMVmmReader
{
    void *context;
    MMWide length;
    /* Exact positional reads. Offsets and length are nonnegative. */
    int (*read_at)(void *, MMWide offset, void *bytes, size_t count);
} MMVmmReader;

typedef struct MMVmmEntry
{
    const char *name; /* Original bytes, counted without the terminator. */
    size_t name_length;
    MMU32 archive_length; /* Zero means an external library. */
    MMWide archive_offset;
} MMVmmEntry;

typedef struct MMVmmDirectory
{
    MMVmmEntry *entries;
    size_t count;
    MMWide movie_offset, movie_length;
} MMVmmDirectory;

/* Read the v1 directory and embedded ranges without loading archive payloads.
 * On success the result owns its entries and names. Pass an empty result and
 * release it with mm_vmm_dispose. On failure the result is untouched. */
MMVmmResult mm_vmm_read(const MMVmmReader *, MMU32 max_entries, MMVmmDirectory *);
void mm_vmm_dispose(MMVmmDirectory *);

typedef struct MMVmmWriter
{
    void *context;
    /* Called only after validating all entry names and the count. */
    int (*begin)(void *);
    int (*write)(void *, const void *bytes, size_t count);
    int (*copy_archive)(void *, size_t entry);
    int (*copy_movie)(void *);
} MMVmmWriter;

/* Emit the complete v1 container in wire order. Archive offsets are ignored.
 * The copy callbacks append exactly the corresponding source ranges. On an
 * I/O failure the host owns disposal of any partial output. */
MMVmmResult mm_vmm_write(const MMVmmWriter *, const MMVmmEntry *, size_t count);
const char *mm_vmm_error(MMVmmResult);

#ifdef __cplusplus
}
#endif
#endif
