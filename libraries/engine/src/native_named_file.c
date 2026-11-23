#include "mm/native_named_file.h"
#include <stdlib.h>

int mm_native_named_file_init(MMNativeNamedFile *file, const MMNativePathChar *path, MMU32 flags,
                              const MMNativeNamedFileDiagnostics *diagnostics, void *context, int *error)
{
    MMNativeMutex *mutex = mm_native_mutex_create(error);
    if (!mutex)
        return 0;
    mm_file_owner_init(&file->state, flags);
    file->mutex = mutex;
    file->path = path;
    file->diagnostics = diagnostics;
    file->diagnostic_context = context;
    return 1;
}

void mm_native_named_file_dispose(MMNativeNamedFile *file)
{
    mm_native_mutex_destroy(file->mutex);
    file->mutex = NULL;
}

void mm_native_named_file_enter(MMNativeNamedFile *file)
{
    if (mm_native_mutex_enter(file->mutex))
        abort();
}

void mm_native_named_file_leave(MMNativeNamedFile *file)
{
    if (mm_native_mutex_leave(file->mutex))
        abort();
}

static void enter(void *p) { mm_native_named_file_enter(p); }
static void leave(void *p) { mm_native_named_file_leave(p); }

static void *open_file(void *p, int create, MMU32 flags)
{
    const MMNativeNamedFile *file = p;
    MMNativeOpenOptions options;
    options.write = (flags & MM_FILE_WRITE) != 0;
    options.deny_read = (flags & MM_FILE_DENY_READ) != 0;
    options.deny_write = (flags & MM_FILE_DENY_WRITE) != 0;
    options.creation = !create ? MM_NATIVE_OPEN_EXISTING :
                       (flags & MM_FILE_TEMP) ? MM_NATIVE_CREATE_EXCLUSIVE : MM_NATIVE_CREATE_ALWAYS;
    return mm_native_file_open(file->path, &options);
}

static void close_file(void *p, void *handle) { (void)p; mm_native_file_close(handle); }
static int flush(void *p, void *handle) { (void)p; return mm_native_file_flush(handle); }
static int remove_file(void *p) { return mm_native_path_delete(((MMNativeNamedFile *)p)->path); }
static int seek(void *p, void *handle, MMU32 offset) { (void)p; return mm_native_file_seek(handle, offset); }
static int resize(void *p, void *handle, MMU32 bytes) { (void)p; return mm_native_file_resize(handle, bytes); }
static int size(void *p, void *handle, MMU32 *bytes) { (void)p; return mm_native_file_size(handle, bytes); }
static int read(void *p, void *handle, void *bytes, MMU32 count)
{
    (void)p;
    return mm_native_file_read(handle, bytes, count);
}

static int write(void *p, void *handle, const void *bytes, MMU32 count)
{
    (void)p;
    return mm_native_file_write(handle, bytes, count);
}

static void issue(void *p, int code)
{
    const MMNativeNamedFile *file = p;
    file->diagnostics->issue(file->diagnostic_context, code);
}

static void validate(void *p, int base)
{
    const MMNativeNamedFile *file = p;
    file->diagnostics->validate(file->diagnostic_context, base);
}

static void arguments(void *p, int operation, const void *data, MMI32 bytes, MMI32 position)
{
    const MMNativeNamedFile *file = p;
    file->diagnostics->arguments(file->diagnostic_context, operation, data, bytes, position);
}

static void assertion(void *p, int code)
{
    const MMNativeNamedFile *file = p;
    file->diagnostics->assertion(file->diagnostic_context, code);
}

MMFileOwnerHost mm_native_named_file_host(MMNativeNamedFile *file)
{
    static const MMFileOwnerHost native = {
        NULL, enter, leave, open_file, close_file, flush, remove_file, seek, resize, size, read, write,
        NULL, NULL, NULL, NULL, 0};
    MMFileOwnerHost host = native;
    host.context = file;
    if (file->diagnostics)
    {
        host.issue = file->diagnostics->issue ? issue : NULL;
        host.validate = file->diagnostics->validate ? validate : NULL;
        host.arguments = file->diagnostics->arguments ? arguments : NULL;
        host.assertion = file->diagnostics->assertion ? assertion : NULL;
        host.debug_read = file->diagnostics->debug_read;
    }
    return host;
}

static int read_at(void *p, MMU32 offset, void *bytes, size_t count)
{
    MMNativeNamedFile *file = p;
    const MMFileOwnerHost host = mm_native_named_file_host(file);
    return offset <= 0x7fffffffUL && count <= 0x7fffffffUL &&
           mm_file_owner_read(&file->state, &host, bytes, (MMI32)count, (MMI32)offset);
}

MMReadStream mm_native_named_file_stream(MMNativeNamedFile *file)
{
    MMReadStream stream;
    const MMFileOwnerHost host = mm_native_named_file_host(file);
    stream.context = file;
    stream.length = mm_file_owner_size(&file->state, &host);
    stream.read_at = read_at;
    return stream;
}
