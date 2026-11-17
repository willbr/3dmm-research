/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mm/native_settings.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef UNICODE
#error POSIX settings use native byte names
#endif
#define MAX_VALUE_BYTES (16UL * 1024 * 1024)
struct MMNativeSettingsContext { pthread_mutex_t mutex; MMU32 low, high; };
typedef struct SettingsStore { int directory, lock; MMNativeSettingsContext *context; } SettingsStore;

MMNativeSettingsContext *mm_native_settings_create(int allow_test_namespace)
{
    MMNativeSettingsContext *context;
    (void)allow_test_namespace;
    context = (MMNativeSettingsContext *)malloc(sizeof(*context));
    if (!context) return NULL;
    if (pthread_mutex_init(&context->mutex, NULL)) { free(context); return NULL; }
    context->low = 0; context->high = 0;
    return context;
}
void mm_native_settings_destroy(MMNativeSettingsContext *context)
{
    if (!context) return;
    pthread_mutex_destroy(&context->mutex); free(context);
}
static int encoded_name(const char *name, char prefix, char *out)
{
    static const char hex[] = "0123456789abcdef";
    size_t count, i;
    unsigned ch;
    if (!name) return 0;
    count = strlen(name);
    if (count > 126) return 0;
    out[0] = prefix; out[1] = '-';
    for (i = 0; i < count; ++i) {
        ch = (unsigned char)name[i];
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        out[2 + i * 2] = hex[ch >> 4]; out[3 + i * 2] = hex[ch & 15];
    }
    out[2 + count * 2] = 0;
    return 1;
}
static void *settings_open(void *opaque, const void *key, int machine, int *created)
{
    MMNativeSettingsContext *context;
    SettingsStore *store;
    char *root, name[255];
    int host, root_fd, directory, lock_fd, result;
    size_t length;
    struct stat info;
    context = (MMNativeSettingsContext *)opaque;
    if (!context || !created) return NULL;
#ifdef __APPLE__
    host = MM_SETTINGS_MACOS;
#else
    host = MM_SETTINGS_LINUX;
#endif
    length = mm_settings_configuration_root(host, machine, getenv("HOME"), getenv("XDG_CONFIG_HOME"), NULL, 0);
    if (!length || !encoded_name((const char *)key, 'k', name)) return NULL;
    root = (char *)malloc(length + 1);
    if (!root) return NULL;
    mm_settings_configuration_root(host, machine, getenv("HOME"), getenv("XDG_CONFIG_HOME"), root, length + 1);
    root_fd = -1; directory = -1; lock_fd = -1; store = NULL;
    if (!mm_native_make_directories(root)) goto done;
    root_fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root_fd < 0 || (mkdirat(root_fd, name, 0700) != 0 && errno != EEXIST)) goto done;
    directory = openat(root_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0) goto done;
    lock_fd = openat(directory, ".lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (lock_fd < 0 || fstat(lock_fd, &info) != 0 || !S_ISREG(info.st_mode)) goto done;
    do { result = flock(lock_fd, LOCK_EX); } while (result != 0 && errno == EINTR);
    if (result != 0) goto done;
    store = (SettingsStore *)malloc(sizeof(*store));
    if (!store) goto done;
    store->directory = directory; store->lock = lock_fd; store->context = context;
    directory = -1; lock_fd = -1;
    *created = 0;
done:
    if (lock_fd >= 0) close(lock_fd);
    if (directory >= 0) close(directory);
    if (root_fd >= 0) close(root_fd);
    free(root);
    return store;
}
static int read_all(int fd, void *data, size_t count)
{
    unsigned char *p;
    ssize_t done;
    p = (unsigned char *)data;
    while (count) {
        done = read(fd, p, count);
        if (done < 0 && errno == EINTR) continue;
        if (done <= 0) return 0;
        p += done; count -= (size_t)done;
    }
    return 1;
}
static int write_all(int fd, const void *data, size_t count)
{
    const unsigned char *p;
    ssize_t done;
    p = (const unsigned char *)data;
    while (count) {
        done = write(fd, p, count);
        if (done < 0 && errno == EINTR) continue;
        if (done <= 0) return 0;
        p += done; count -= (size_t)done;
    }
    return 1;
}
static int at_end(int fd)
{
    unsigned char extra;
    ssize_t result;
    do { result = read(fd, &extra, 1); } while (result < 0 && errno == EINTR);
    return result == 0;
}
static int sync_file(int fd)
{
    int result;
    do { result = fsync(fd); } while (result != 0 && errno == EINTR);
    return result == 0;
}
static int valid_payload(int type, const void *data, MMU32 bytes)
{
    return bytes <= MAX_VALUE_BYTES && (!bytes || data) &&
        (type == MM_SETTINGS_BINARY || (type == MM_SETTINGS_NUMBER && bytes == 4) ||
         (type == MM_SETTINGS_STRING && bytes && memchr(data, 0, bytes)));
}
static int settings_read(void *context, void *handle, const void *name, void *data, MMU32 *bytes, int *type)
{
    SettingsStore *store;
    char file[255];
    int fd, result, kind;
    struct stat info;
    unsigned char header[12], *payload;
    MMU32 length;
    (void)context;
    if (!handle || !bytes || !type || !encoded_name((const char *)name, 'v', file)) return MM_SETTINGS_ERROR;
    store = (SettingsStore *)handle;
    fd = openat(store->directory, file, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT ? MM_SETTINGS_MISSING : MM_SETTINGS_ERROR;
    result = MM_SETTINGS_ERROR; payload = NULL;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || !read_all(fd, header, sizeof(header)) ||
        memcmp(header, "KPS1", 4) || header[5] || header[6] || header[7]) goto done;
    length = (MMU32)header[8] | ((MMU32)header[9] << 8) | ((MMU32)header[10] << 16) | ((MMU32)header[11] << 24);
    if (length > MAX_VALUE_BYTES || info.st_size != (off_t)(sizeof(header) + length)) goto done;
    kind = header[4] == 1 ? MM_SETTINGS_NUMBER : header[4] == 2 ? MM_SETTINGS_STRING :
        header[4] == 3 ? MM_SETTINGS_BINARY : MM_SETTINGS_UNKNOWN;
    if (data && *bytes < length) { *bytes = length; goto done; }
    if (length) { payload = (unsigned char *)malloc(length); if (!payload) goto done; }
    if (!read_all(fd, payload, length) || !at_end(fd) || !valid_payload(kind, payload, length)) goto done;
    if (data && length) memcpy(data, payload, length);
    *bytes = length; *type = kind; result = MM_SETTINGS_FOUND;
done:
    free(payload); close(fd);
    return result;
}
/* Decimal conversion of the two-word counter, retaining the prior 64-bit
 * sequence/wrap contract without C99 integers or a process-global counter. */
static void decimal_counter(MMU32 low, MMU32 high, char *out)
{
    char reversed[20];
    unsigned count, bit, remainder;
    MMU32 qlow, qhigh;
    count = 0;
    do {
        qlow = 0; qhigh = 0; remainder = 0;
        for (bit = 64; bit > 0; --bit) {
            remainder = remainder * 2 + (unsigned)((bit > 32 ? high >> (bit - 33) : low >> (bit - 1)) & 1);
            if (remainder >= 10) {
                remainder -= 10;
                if (bit > 32) qhigh |= (MMU32)1 << (bit - 33);
                else qlow |= (MMU32)1 << (bit - 1);
            }
        }
        reversed[count++] = (char)('0' + remainder);
        low = qlow; high = qhigh;
    } while (low || high);
    while (count) *out++ = reversed[--count];
    *out = 0;
}
static int temporary_name(MMNativeSettingsContext *context, char *out)
{
    MMU32 low, high;
    if (pthread_mutex_lock(&context->mutex)) return 0;
    low = context->low; high = context->high;
    if (++context->low == 0) ++context->high;
    pthread_mutex_unlock(&context->mutex);
    sprintf(out, ".tmp-%ld-", (long)getpid());
    decimal_counter(low, high, out + strlen(out));
    return 1;
}
static int settings_write(void *context, void *handle, const void *name, const void *data, MMU32 bytes, int type)
{
    SettingsStore *store;
    char file[255], candidate[64];
    int fd, ok, named, directory, closed;
    unsigned attempt, i;
    unsigned char header[12] = {'K', 'P', 'S', '1', 0, 0, 0, 0, 0, 0, 0, 0};
    (void)context;
    if (!handle || !valid_payload(type, data, bytes) || !encoded_name((const char *)name, 'v', file)) return 0;
    store = (SettingsStore *)handle; directory = store->directory;
    fd = -1; named = 0; ok = 0;
    for (attempt = 0; attempt < 128; ++attempt) {
        if (!temporary_name(store->context, candidate)) goto done;
        fd = openat(directory, candidate, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0) { named = 1; break; }
        if (errno != EEXIST) goto done;
    }
    if (fd < 0) goto done;
    header[4] = (unsigned char)(type == MM_SETTINGS_NUMBER ? 1 : type == MM_SETTINGS_STRING ? 2 : 3);
    for (i = 0; i < 4; ++i) header[8 + i] = (unsigned char)(bytes >> (8 * i));
    if (!write_all(fd, header, sizeof(header)) || !write_all(fd, data, bytes) || !sync_file(fd)) goto done;
    closed = close(fd); fd = -1;
    if (closed != 0 || renameat(directory, candidate, directory, file) != 0) goto done;
    named = 0; sync_file(directory); ok = 1;
done:
    if (named) unlinkat(directory, candidate, 0);
    if (fd >= 0) close(fd);
    return ok;
}
static void settings_close(void *context, void *handle)
{
    SettingsStore *store;
    (void)context;
    store = (SettingsStore *)handle;
    if (!store) return;
    /* Store's prior member destruction closed lock before directory. */
    close(store->lock); close(store->directory); free(store);
}
void mm_native_settings_backend(MMNativeSettingsContext *context, MMSettingsBackend *backend)
{
    backend->context = context; backend->open = settings_open; backend->read = settings_read;
    backend->write = settings_write; backend->close = settings_close;
}
