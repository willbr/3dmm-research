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

enum
{
    MM_ARCHIVE_PATH_OK, MM_ARCHIVE_PATH_EMPTY, MM_ARCHIVE_PATH_TOO_LONG,
    MM_ARCHIVE_PATH_BAD_UTF8, MM_ARCHIVE_PATH_ENCODE, MM_ARCHIVE_PATH_LOSSY
};

/* Produce the terminated short path used by the legacy file API. ANSI paths
 * require an exact code-page round trip. Other paths retain their input bytes.
 * Output storage has room for MM_STRING_MAX + 1 bytes. */
int mm_native_archive_legacy_path(const char *, size_t, int ansi, const MMTextHost *, char *);

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
/* A profile without handle services uses the C archive handle provider. */
MMResourceCache *mm_native_archive_source_cache(const MMNativeArchiveSource *, MMI32 budget,
                                                 const MMResourceServices *,
                                                 const MMStorageAllocator *owner, const MMStorageAllocator *storage);

enum { MM_NATIVE_ARCHIVE_STORED, MM_NATIVE_ARCHIVE_PREVIEW, MM_NATIVE_ARCHIVE_LIVE };
enum
{
    MM_NATIVE_ARCHIVE_INPUT_OK, MM_NATIVE_ARCHIVE_INPUT_OPEN, MM_NATIVE_ARCHIVE_INPUT_LIVE_INDEX,
    MM_NATIVE_ARCHIVE_INPUT_MEMORY
};

typedef struct MMNativeArchiveInput
{
    MMNativeArchiveSource stored;
    MMResourceCache *live;
    struct MMChunkOwnerRead *live_reader;
    MMArchiveReader *reader;
    int mode;
} MMNativeArchiveInput;

typedef struct MMNativeArchiveInputHost
{
    void *context;
    /* Return an owned cache and its borrowed reader, or select the stored file.
     * Setting selected without returning a cache reports a live-open failure. */
    MMResourceCache *(*open_live)(void *, int prefer_working, int *selected, struct MMChunkOwnerRead **);
    int (*open_stored)(void *, MMNativeArchiveSource *);
} MMNativeArchiveInputHost;

/* Open the cache used for source tags. Once selected, a live archive's
 * result is authoritative, including an open failure. Media sources and
 * removable paths prefer the working archive. */
MMResourceCache *mm_native_archive_open_cache(const MMNativeArchiveInputHost *, int media, int removable,
                                              MMI32 budget, const MMResourceServices *,
                                              const MMStorageAllocator *owner, const MMStorageAllocator *storage);

/* Initialize input to zero. A failed open leaves it intact. A successful
 * input owns the reader and its live cache or stored file and index.
 * Host callbacks run synchronously and must not unwind. */
int mm_native_archive_input_open(MMNativeArchiveInput *, int mode, const MMArchiveReaderOptions *,
                                 const MMNativeArchiveInputHost *);
/* Open a stored archive by UTF-8 path. Return a raw MMArchiveReadResult.
 * Failure leaves the existing input intact. */
int mm_native_archive_input_open_utf8(MMNativeArchiveInput *, const char *, int expanded, MMU32 max_name_bytes,
                                      const MMArchiveReaderOptions *);
void mm_native_archive_input_close(MMNativeArchiveInput *);
MMMovieSessionGraph mm_native_archive_input_graph(const MMNativeArchiveInput *);

/* Select the native code page and the legacy name writer for the wire encoding.
 * The caller supplies its archive dialect and diagnostic mode. */
MMPrivateArchiveProfile mm_native_archive_profile(int encoding, int swapped_unicode, int expanded, int trash_unused);
/* Open a read-only, editable or fresh private archive using the same file provider. */
MMPrivateArchive *mm_native_private_archive_open(const MMNativePathChar *, int mode,
                                                 const MMPrivateArchiveProfile *);
MMPrivateArchive *mm_native_private_archive_open_utf8(const char *, int mode, const MMPrivateArchiveProfile *);
/* Own an empty writable archive backed by a private, non-publishable file. */
MMPrivateArchive *mm_native_private_archive_temporary(const MMPrivateArchiveProfile *);

#ifdef __cplusplus
}
#endif
#endif
