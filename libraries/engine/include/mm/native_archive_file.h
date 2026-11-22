#ifndef MM_NATIVE_ARCHIVE_FILE_H
#define MM_NATIVE_ARCHIVE_FILE_H
#include "native_files.h"
#include "archive_reader.h"
#include "private_archive.h"
#include "resource_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMNativeArchiveFile MMNativeArchiveFile;

/* Open with one owned reference. Streams borrow that reference, while blocks
 * retain their own. Access to the shared native file position is serialized. */
MMNativeArchiveFile *mm_native_archive_file_open(const MMNativePathChar *);
void mm_native_archive_file_release(MMNativeArchiveFile *);
int mm_native_archive_file_stream(MMNativeArchiveFile *, MMReadStream *);
const MMBlockFileServices *mm_native_archive_file_blocks(void);

/* Own a validated wire index and its matching physical file. Initialize to
 * zero before the first open. Open publishes atomically and a failed open
 * leaves the existing source intact. Close readers before reopening or
 * clearing the source. Each cache retains its own index and file references. */
typedef struct MMNativeArchiveSource
{
    MMNativeArchiveFile *file;
    MMChunkIndex *index;
    MMReadStream stream;
} MMNativeArchiveSource;

int mm_native_archive_source_open(MMNativeArchiveSource *, const MMNativePathChar *,
                                  int expanded, MMU32 max_name_bytes);
void mm_native_archive_source_clear(MMNativeArchiveSource *);
int mm_native_archive_source_reader(const MMNativeArchiveSource *, const MMArchiveReaderOptions *, MMArchiveReader **);
MMResourceCache *mm_native_archive_source_cache(const MMNativeArchiveSource *, MMI32 budget,
                                                 const MMResourceServices *,
                                                 const MMStorageAllocator *owner, const MMStorageAllocator *storage);

/* Open a read-only, editable or fresh private archive using the same file provider. */
MMPrivateArchive *mm_native_private_archive_open(const MMNativePathChar *, int mode,
                                                 const MMPrivateArchiveProfile *);

#ifdef __cplusplus
}
#endif
#endif
