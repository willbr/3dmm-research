#ifndef MM_CONTENT_VFS_H
#define MM_CONTENT_VFS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMVfs MMVfs;
    typedef struct MMVfsFile MMVfsFile;
    typedef struct MMVfsPosition
    {
        unsigned long high, low; /* Each carries 32 significant bits, without C99 types. */
    } MMVfsPosition;
    typedef struct MMVfsProvider
    {
        void *context;
        void *(*open)(void *context, const char *location);
        size_t (*read_at)(void *context, void *file, MMVfsPosition position, void *buffer, size_t capacity);
        void (*close)(void *context, void *file);
    } MMVfsProvider;
    typedef struct MMVfsMount
    {
        const char *name, *location;
    } MMVfsMount;
    enum MMVfsError
    {
        MM_VFS_OK,
        MM_VFS_MEMORY,
        MM_VFS_NAME,
        MM_VFS_CASE,
        MM_VFS_MISSING,
        MM_VFS_IO
    };
    /* Names preserve their original bytes and use ASCII case folding. Physical
     * locations are opaque to the core; the host supplies platform I/O. Provider
     * context must outlive open files. NULL permits lookup without I/O. */
    MMVfs *mm_vfs_create(const MMVfsProvider *provider);
    void mm_vfs_destroy(MMVfs *vfs);
    const char *mm_vfs_error(int error);
    int mm_vfs_source_key(const char *title, const char *folder, char *output, size_t capacity);
    /* Transactional. First mount wins for identical virtual names. Ambiguous
     * casing and path traversal names are rejected. replace clears this source. */
    int mm_vfs_mount(MMVfs *vfs, const char *source, const MMVfsMount *files, size_t count,
                           int replace);
    int mm_vfs_mounted(const MMVfs *vfs, const char *source);
    /* Results are borrowed until mutation/destruction. Start cursor at zero.
     * Enumeration is sorted by virtual filename, independently of physical paths. */
    int mm_vfs_next(const MMVfs *vfs, const char *source, size_t *cursor, MMVfsMount *file);
    /* Same order and lifetime, with an ASCII-insensitive extension filter.
     * Include the leading dot in extension. */
    int mm_vfs_next_extension(const MMVfs *, const char *source, const char *extension,
                              size_t *cursor, MMVfsMount *);
    const char *mm_vfs_resolve(const MMVfs *vfs, const char *source, const char *name);
    MMVfsFile *mm_vfs_open(const MMVfs *vfs, const char *source, const char *name);
    /* Open handles survive unmounts and VFS destruction; close each explicitly. */
    size_t mm_vfs_read(MMVfsFile *file, void *buffer, size_t capacity);
    int mm_vfs_seek(MMVfsFile *file, MMVfsPosition position);
    MMVfsPosition mm_vfs_tell(const MMVfsFile *file);
    void mm_vfs_close(MMVfsFile *file);
#ifdef __cplusplus
}
#endif
#endif
