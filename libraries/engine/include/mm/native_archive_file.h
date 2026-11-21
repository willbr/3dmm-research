#ifndef MM_NATIVE_ARCHIVE_FILE_H
#define MM_NATIVE_ARCHIVE_FILE_H
#include "native_files.h"
#include "private_archive.h"

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

/* Open a read-only, editable or fresh private archive using the same file provider. */
MMPrivateArchive *mm_native_private_archive_open(const MMNativePathChar *, int mode,
                                                 const MMPrivateArchiveProfile *);

#ifdef __cplusplus
}
#endif
#endif
