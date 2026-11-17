/* Exercise retry and two-word sequence boundaries in the real C provider. */
#if !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mm/native_settings.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int fail_allocation, allocations, read_interrupt, write_interrupt, sync_interrupt;
static unsigned read_calls, write_calls, sync_calls;
static void *allocate(size_t bytes)
{ ++allocations; return fail_allocation == allocations ? NULL : malloc(bytes); }
static ssize_t read_retry(int fd, void *data, size_t count)
{
    ++read_calls;
    if (read_interrupt) { read_interrupt = 0; errno = EINTR; return -1; }
    return read(fd, data, count > 1 ? 1 : count);
}
static ssize_t write_retry(int fd, const void *data, size_t count)
{
    ++write_calls;
    if (write_interrupt) { write_interrupt = 0; errno = EINTR; return -1; }
    return write(fd, data, count > 1 ? 1 : count);
}
static int sync_retry(int fd)
{
    ++sync_calls;
    if (sync_interrupt) { sync_interrupt = 0; errno = EINTR; return -1; }
    return fsync(fd);
}
#define malloc allocate
#define read(fd, data, count) read_retry(fd, data, count)
#define write(fd, data, count) write_retry(fd, data, count)
#define fsync sync_retry
#include "../libraries/engine/src/native_settings_posix.c"
#undef malloc
#undef read
#undef write
#undef fsync
int main(void)
{
    MMNativeSettingsContext *context;
    MMSettingsBackend backend;
    void *handle;
    MMU32 value, bytes;
    int created, kind, before, after;
    char pattern[] = "/tmp/3mm-c89-settings-fail-XXXXXX", root[512], directory[512], file[1024];
    int pipe_fd[2];
    char output[64], copied[4], encoded[255], long_name[128];
    decimal_counter(0, 0, output); assert(!strcmp(output, "0"));
    decimal_counter(0xffffffffUL, 0, output); assert(!strcmp(output, "4294967295"));
    decimal_counter(0, 1, output); assert(!strcmp(output, "4294967296"));
    decimal_counter(0xffffffffUL, 0xffffffffUL, output); assert(!strcmp(output, "18446744073709551615"));
    allocations = 0; fail_allocation = 1; assert(!mm_native_settings_create(0));
    fail_allocation = 0; context = mm_native_settings_create(0); assert(context);
    context->low = 0xffffffffUL; context->high = 0;
    assert(temporary_name(context, output) && !strcmp(strrchr(output, '-') + 1, "4294967295"));
    assert(temporary_name(context, output) && !strcmp(strrchr(output, '-') + 1, "4294967296"));
    context->low = context->high = 0xffffffffUL;
    assert(temporary_name(context, output) && !strcmp(strrchr(output, '-') + 1, "18446744073709551615"));
    assert(temporary_name(context, output) && !strcmp(strrchr(output, '-') + 1, "0"));
    assert(encoded_name("A/\\\xff", 'k', encoded) && !strcmp(encoded, "k-612f5cff"));
    assert(encoded_name("", 'v', encoded) && !strcmp(encoded, "v-"));
    memset(long_name, 'x', 126); long_name[126] = 0;
    assert(encoded_name(long_name, 'k', encoded) && strlen(encoded) == 254);
    long_name[126] = 'x'; long_name[127] = 0;
    assert(!encoded_name(long_name, 'k', encoded) && !encoded_name(NULL, 'k', encoded));
    assert(pipe(pipe_fd) == 0);
    write_interrupt = 1;
    assert(write_all(pipe_fd[1], "abcd", 4) && write_calls == 5);
    assert(close(pipe_fd[1]) == 0);
    read_interrupt = 1;
    assert(read_all(pipe_fd[0], copied, 4) && read_calls == 5 && !memcmp(copied, "abcd", 4));
    read_interrupt = 1;
    assert(at_end(pipe_fd[0]) && read_calls == 7);
    assert(!read_all(pipe_fd[0], copied, 1));
    close(pipe_fd[0]);
    sync_interrupt = 1;
    assert(!sync_file(-1) && sync_calls == 2);
    assert(mkdtemp(pattern));
    assert(setenv("XDG_CONFIG_HOME", pattern, 1) == 0 && setenv("HOME", pattern, 1) == 0);
    mm_native_settings_backend(context, &backend);
    for (fail_allocation = 1; fail_allocation <= 2; ++fail_allocation) {
        before = dup(STDOUT_FILENO); assert(before >= 0); close(before);
        allocations = 0; created = 77;
        assert(!backend.open(backend.context, "Test", 0, &created) && created == 77);
        after = dup(STDOUT_FILENO); assert(after == before); close(after);
    }
    fail_allocation = 0;
    handle = backend.open(backend.context, "Test", 0, &created); assert(handle && !created);
    value = 42;
    assert(backend.write(backend.context, handle, "Value", &value, 4, MM_SETTINGS_NUMBER));
    allocations = 0; fail_allocation = 1; value = 77; bytes = 4; kind = 88;
    assert(backend.read(backend.context, handle, "Value", &value, &bytes, &kind) == MM_SETTINGS_ERROR);
    assert(value == 77 && bytes == 4 && kind == 88);
    fail_allocation = 0;
    assert(backend.read(backend.context, handle, "Value", &value, &bytes, &kind) == MM_SETTINGS_FOUND);
    assert(value == 42 && bytes == 4 && kind == MM_SETTINGS_NUMBER);
    backend.close(backend.context, handle);
#ifdef __APPLE__
    assert(mm_settings_configuration_root(MM_SETTINGS_MACOS, 0, pattern, pattern, root, sizeof(root)));
#else
    assert(mm_settings_configuration_root(MM_SETTINGS_LINUX, 0, pattern, pattern, root, sizeof(root)));
#endif
    sprintf(directory, "%s/k-74657374", root);
    sprintf(file, "%s/v-76616c7565", directory); assert(unlink(file) == 0);
    sprintf(file, "%s/.lock", directory); assert(unlink(file) == 0);
    assert(rmdir(directory) == 0 && rmdir(root) == 0);
#ifdef __APPLE__
    sprintf(file, "%s/Library/Application Support", pattern); assert(rmdir(file) == 0);
    sprintf(file, "%s/Library", pattern); assert(rmdir(file) == 0);
#endif
    assert(rmdir(pattern) == 0);
    mm_native_settings_destroy(context);
    return 0;
}
