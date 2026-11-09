#include "three_mm/content_vfs.h"
#include <stdlib.h>
#include <string.h>

typedef struct Entry
{
    char *source, *name, *location;
} Entry;
struct ThreeMMVfs
{
    Entry *entries;
    size_t count;
    ThreeMMVfsProvider provider;
};
struct ThreeMMVfsFile
{
    ThreeMMVfsProvider provider;
    void *handle;
    ThreeMMVfsPosition position;
};
static unsigned char fold(unsigned char ch)
{
    return ch >= 'A' && ch <= 'Z' ? (unsigned char)(ch + ('a' - 'A')) : ch;
}
static int equal(const char *a, const char *b)
{
    while (*a && *b && fold((unsigned char)*a) == fold((unsigned char)*b))
    {
        ++a;
        ++b;
    }
    return *a == *b;
}
static int span_equal(const char *text, size_t length, const char *other)
{
    size_t i;
    if (strlen(other) != length)
        return 0;
    for (i = 0; i < length; ++i)
        if (fold((unsigned char)text[i]) != (unsigned char)other[i])
            return 0;
    return 1;
}
static const char *known(const char *text, size_t length)
{
    static const char *aliases[][2] = {{"3dmovie", "3dmovie"},
                                       {"3d movie maker", "3dmovie"},
                                       {"3dmm", "3dmovie"},
                                       {"n3dmmexp", "n3dmmexp"},
                                       {"n3dmm expansion", "n3dmmexp"},
                                       {"nickelodeon", "n3dmmexp"},
                                       {"doraemon", "doraemon"},
                                       {"doraemon character kit", "doraemon"},
                                       {"expansions", "expansions"},
                                       {"3dmm expansion pack", "expansions"},
                                       {"frankie's expansion", "expansions"},
                                       {"v3dmm", "v3dmm"},
                                       {"virtual3dmm", "v3dmm"}};
    size_t i;
    for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i)
        if (span_equal(text, length, aliases[i][0]))
            return aliases[i][1];
    return NULL;
}
int three_mm_vfs_source_key(const char *title, const char *folder, char *output, size_t capacity)
{
    const char *slash, *backslash, *key;
    size_t title_length, folder_length, length, i;
    if (!title || !output || !capacity)
        return 0;
    if (!folder)
        folder = "";
    title_length = strlen(title);
    folder_length = strlen(folder);
    if (!folder_length)
    {
        slash = strrchr(title, '/');
        backslash = strrchr(title, '\\');
        if (backslash && (!slash || backslash > slash))
            slash = backslash;
        if (slash)
        {
            title_length = (size_t)(slash - title);
            folder = slash + 1;
            folder_length = strlen(folder);
        }
    }
    key = known(folder, folder_length);
    if (!key)
        key = known(title, title_length);
    if (key)
        length = strlen(key);
    else
    {
        key = folder_length ? folder : title;
        length = folder_length ? folder_length : title_length;
    }
    if (!length || length >= capacity)
        return 0;
    for (i = 0; i < length; ++i)
        output[i] = (char)fold((unsigned char)key[i]);
    output[length] = 0;
    return 1;
}
static char *source_key(const char *source)
{
    char *key;
    size_t capacity;
    if (!source || strlen(source) > (size_t)-1 - 32)
        return NULL;
    capacity = strlen(source) + 32;
    key = (char *)malloc(capacity);
    if (key && !three_mm_vfs_source_key(source, NULL, key, capacity))
    {
        free(key);
        key = NULL;
    }
    return key;
}
static char *copy(const char *text)
{
    char *result;
    size_t length;
    length = strlen(text) + 1;
    result = (char *)malloc(length);
    if (result)
        memcpy(result, text, length);
    return result;
}
static void release(Entry *entries, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i)
    {
        free(entries[i].source);
        free(entries[i].name);
        free(entries[i].location);
    }
    free(entries);
}
static int append(Entry *entry, const char *source, const char *name, const char *location)
{
    entry->source = copy(source);
    entry->name = copy(name);
    entry->location = copy(location);
    return entry->source && entry->name && entry->location;
}
static int compare(const void *left, const void *right)
{
    const Entry *a, *b;
    int order;
    a = (const Entry *)left;
    b = (const Entry *)right;
    order = strcmp(a->source, b->source);
    return order ? order : strcmp(a->name, b->name);
}
ThreeMMVfs *three_mm_vfs_create(const ThreeMMVfsProvider *provider)
{
    ThreeMMVfs *vfs;
    vfs = (ThreeMMVfs *)calloc(1, sizeof(*vfs));
    if (vfs && provider)
        vfs->provider = *provider;
    return vfs;
}
void three_mm_vfs_destroy(ThreeMMVfs *vfs)
{
    if (vfs)
    {
        release(vfs->entries, vfs->count);
        free(vfs);
    }
}
const char *three_mm_vfs_error(int error)
{
    static const char *messages[] = {"Success",
                                     "Insufficient memory for content mounts",
                                     "Invalid virtual content name",
                                     "Content mounts have ambiguous filename casing",
                                     "Content file is not mounted",
                                     "Cannot open mounted content"};
    return error >= 0 && error < (int)(sizeof(messages) / sizeof(messages[0])) ? messages[error] : "VFS error";
}
int three_mm_vfs_mount(ThreeMMVfs *vfs, const char *source, const ThreeMMVfsMount *files, size_t count, int replace)
{
    Entry *next;
    char *key;
    size_t capacity, used, i, j;
    int error;
    if (!vfs || !source || !source[0] || (count && !files))
        return THREE_MM_VFS_NAME;
    if (count > (size_t)-1 / sizeof(Entry) - vfs->count)
        return THREE_MM_VFS_MEMORY;
    capacity = vfs->count + count;
    next = (Entry *)calloc(capacity ? capacity : 1, sizeof(*next));
    key = source_key(source);
    error = THREE_MM_VFS_MEMORY;
    used = 0;
    if (!next || !key)
        goto fail;
    for (i = 0; i < vfs->count; ++i)
        if (!replace || strcmp(vfs->entries[i].source, key))
        {
            if (!append(&next[used], vfs->entries[i].source, vfs->entries[i].name, vfs->entries[i].location))
                goto fail;
            ++used;
        }
    for (i = 0; i < count; ++i)
    {
        if (!files[i].name || !files[i].name[0] || !files[i].location || !files[i].location[0] ||
            !strcmp(files[i].name, ".") || !strcmp(files[i].name, "..") || strpbrk(files[i].name, "/\\:"))
        {
            error = THREE_MM_VFS_NAME;
            goto fail;
        }
        for (j = 0; j < used; ++j)
            if (!strcmp(next[j].source, key) && equal(next[j].name, files[i].name))
                break;
        if (j < used)
        {
            if (strcmp(next[j].name, files[i].name))
            {
                error = THREE_MM_VFS_CASE;
                goto fail;
            }
            continue;
        }
        if (!append(&next[used], key, files[i].name, files[i].location))
            goto fail;
        ++used;
    }
    qsort(next, used, sizeof(*next), compare);
    release(vfs->entries, vfs->count);
    vfs->entries = next;
    vfs->count = used;
    free(key);
    return THREE_MM_VFS_OK;
fail:
    if (next)
        release(next, capacity);
    free(key);
    return error;
}
int three_mm_vfs_next(const ThreeMMVfs *vfs, const char *source, size_t *cursor, ThreeMMVfsMount *file)
{
    char *key;
    const Entry *entry;
    if (!vfs || !cursor || !file)
        return 0;
    key = source_key(source);
    if (!key)
        return 0;
    while (*cursor < vfs->count)
    {
        entry = &vfs->entries[(*cursor)++];
        if (!strcmp(key, entry->source))
        {
            file->name = entry->name;
            file->location = entry->location;
            free(key);
            return 1;
        }
    }
    free(key);
    return 0;
}
int three_mm_vfs_mounted(const ThreeMMVfs *vfs, const char *source)
{
    size_t cursor;
    ThreeMMVfsMount file;
    cursor = 0;
    return three_mm_vfs_next(vfs, source, &cursor, &file);
}
const char *three_mm_vfs_resolve(const ThreeMMVfs *vfs, const char *source, const char *name)
{
    size_t cursor;
    ThreeMMVfsMount file;
    if (!name)
        return NULL;
    cursor = 0;
    while (three_mm_vfs_next(vfs, source, &cursor, &file))
        if (equal(name, file.name))
            return file.location;
    return NULL;
}
ThreeMMVfsFile *three_mm_vfs_open(const ThreeMMVfs *vfs, const char *source, const char *name)
{
    const char *location;
    ThreeMMVfsFile *file;
    if (!vfs || !vfs->provider.open || !vfs->provider.read_at || !vfs->provider.close)
        return NULL;
    location = three_mm_vfs_resolve(vfs, source, name);
    if (!location)
        return NULL;
    file = (ThreeMMVfsFile *)calloc(1, sizeof(*file));
    if (!file)
        return NULL;
    file->provider = vfs->provider;
    file->handle = file->provider.open(file->provider.context, location);
    if (!file->handle)
    {
        free(file);
        return NULL;
    }
    return file;
}
size_t three_mm_vfs_read(ThreeMMVfsFile *file, void *buffer, size_t capacity)
{
    size_t count;
    unsigned long old;
    if (!file || !capacity || !buffer)
        return 0;
    if (capacity > 0xffffffffUL)
        capacity = 0xffffffffUL;
    if (file->position.high == 0xffffffffUL && capacity > 0xffffffffUL - file->position.low)
        capacity = (size_t)(0xffffffffUL - file->position.low);
    count = file->provider.read_at(file->provider.context, file->handle, file->position, buffer, capacity);
    if (count > capacity)
        return 0;
    old = file->position.low;
    file->position.low = (old + (unsigned long)count) & 0xffffffffUL;
    if ((unsigned long)count > 0xffffffffUL - old)
        ++file->position.high;
    return count;
}
int three_mm_vfs_seek(ThreeMMVfsFile *file, ThreeMMVfsPosition position)
{
    if (!file || position.high > 0xffffffffUL || position.low > 0xffffffffUL)
        return 0;
    file->position = position;
    return 1;
}
ThreeMMVfsPosition three_mm_vfs_tell(const ThreeMMVfsFile *file)
{
    ThreeMMVfsPosition zero;
    zero.high = zero.low = 0;
    return file ? file->position : zero;
}
void three_mm_vfs_close(ThreeMMVfsFile *file)
{
    if (file)
    {
        file->provider.close(file->provider.context, file->handle);
        free(file);
    }
}
