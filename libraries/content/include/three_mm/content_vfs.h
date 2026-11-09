#ifndef THREE_MM_CONTENT_VFS_H
#define THREE_MM_CONTENT_VFS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMVfs ThreeMMVfs;
    typedef struct ThreeMMVfsFile ThreeMMVfsFile;
    typedef struct ThreeMMVfsPosition
    {
        unsigned long high, low; /* Each carries 32 significant bits, without C99 types. */
    } ThreeMMVfsPosition;
    typedef struct ThreeMMVfsProvider
    {
        void *context;
        void *(*open)(void *context, const char *location);
        size_t (*read_at)(void *context, void *file, ThreeMMVfsPosition position, void *buffer, size_t capacity);
        void (*close)(void *context, void *file);
    } ThreeMMVfsProvider;
    typedef struct ThreeMMVfsMount
    {
        const char *name, *location;
    } ThreeMMVfsMount;
    enum ThreeMMVfsError
    {
        THREE_MM_VFS_OK,
        THREE_MM_VFS_MEMORY,
        THREE_MM_VFS_NAME,
        THREE_MM_VFS_CASE,
        THREE_MM_VFS_MISSING,
        THREE_MM_VFS_IO
    };
    /* Names preserve their original bytes and use ASCII case folding. Physical
     * locations are opaque to the core; the host supplies platform I/O. Provider
     * context must outlive open files. NULL permits lookup without I/O. */
    ThreeMMVfs *three_mm_vfs_create(const ThreeMMVfsProvider *provider);
    void three_mm_vfs_destroy(ThreeMMVfs *vfs);
    const char *three_mm_vfs_error(int error);
    int three_mm_vfs_source_key(const char *title, const char *folder, char *output, size_t capacity);
    /* Transactional. First mount wins for identical virtual names. Ambiguous
     * casing and path traversal names are rejected. replace clears this source. */
    int three_mm_vfs_mount(ThreeMMVfs *vfs, const char *source, const ThreeMMVfsMount *files, size_t count,
                           int replace);
    int three_mm_vfs_mounted(const ThreeMMVfs *vfs, const char *source);
    /* Results are borrowed until mutation/destruction. Start cursor at zero.
     * Enumeration is sorted by virtual filename, independently of physical paths. */
    int three_mm_vfs_next(const ThreeMMVfs *vfs, const char *source, size_t *cursor, ThreeMMVfsMount *file);
    const char *three_mm_vfs_resolve(const ThreeMMVfs *vfs, const char *source, const char *name);
    ThreeMMVfsFile *three_mm_vfs_open(const ThreeMMVfs *vfs, const char *source, const char *name);
    /* Open handles survive unmounts and VFS destruction; close each explicitly. */
    size_t three_mm_vfs_read(ThreeMMVfsFile *file, void *buffer, size_t capacity);
    int three_mm_vfs_seek(ThreeMMVfsFile *file, ThreeMMVfsPosition position);
    ThreeMMVfsPosition three_mm_vfs_tell(const ThreeMMVfsFile *file);
    void three_mm_vfs_close(ThreeMMVfsFile *file);
#ifdef __cplusplus
}
#endif
#endif
