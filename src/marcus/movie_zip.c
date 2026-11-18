/* Licensed under the MIT License. */
#include "movie_zip.h"
#include "player.h"
#include "mm/vxp_package.h"
#include <stdlib.h>
#include <string.h>

#define ZIP_NAME_CAPACITY 65536UL
#define MOVIE_BYTES (512UL * 1024 * 1024)
#define FONT_BYTES (64UL * 1024 * 1024)

static const char *suffix(const char *name, size_t length)
{
    while (length)
        if (name[--length] == '.')
            return name + length;
    return NULL;
}

static const char *font_extension(const char *extension)
{
    static const char *const extensions[] = {".ttf", ".otf", ".ttc", ".otc", ".fon", ".fnt", ".dfont"};
    size_t i;
    if (extension)
        for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i)
            if (!mm_vxp_compare_names(extension, extensions[i]))
                return extensions[i];
    return NULL;
}

int marcus_zip_signature(const char *filename, const char signature[4])
{
    const char *extension = suffix(filename, strlen(filename));
    return (extension && !mm_vxp_compare_names(extension, ".zip")) ||
           (signature[0] == 'P' && signature[1] == 'K' &&
            ((signature[2] == 3 && signature[3] == 4) || (signature[2] == 5 && signature[3] == 6)));
}

int marcus_zip_is_font(const char *filename)
{
    return font_extension(suffix(filename, strlen(filename))) != NULL;
}

static int safe_name(const char *name, size_t length)
{
    size_t i, begin = 0;
    if (!length || name[0] == '/' || name[0] == '\\' || memchr(name, ':', length) || memchr(name, '\0', length))
        return 0;
    for (i = 0; i <= length; ++i)
    {
        if (i < length && name[i] != '/' && name[i] != '\\')
            continue;
        if (i - begin == 2 && name[begin] == '.' && name[begin + 1] == '.')
            return 0;
        begin = i + 1;
    }
    return 1;
}

static void clear_list(MarcusZipList *list)
{
    MMU32 i;
    for (i = 0; i < list->count; ++i)
        free(list->items[i].name);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

void marcus_zip_catalog_clear(MarcusZipCatalog *catalog)
{
    clear_list(&catalog->movies);
    clear_list(&catalog->fonts);
}

static int append(MarcusZipList *list, const MarcusZipEntry *entry, const char *name, size_t length)
{
    MMU32 capacity;
    MarcusZipEntry *items;
    char *copy;
    if (list->count == list->capacity)
    {
        capacity = list->capacity ? list->capacity * 2 : 8;
        if ((size_t)capacity > (size_t)-1 / sizeof(*items))
            return 0;
        items = (MarcusZipEntry *)realloc(list->items, (size_t)capacity * sizeof(*items));
        if (!items)
            return 0;
        list->items = items;
        list->capacity = capacity;
    }
    copy = (char *)malloc(length);
    if (!copy)
        return 0;
    memcpy(copy, name, length);
    list->items[list->count] = *entry;
    list->items[list->count++].name = copy;
    return 1;
}

static const char *read_member(MMZip *zip, MMU32 slot, char *name, MarcusZipCatalog *catalog)
{
    MarcusZipEntry entry;
    size_t length;
    const char *extension, *font;
    if (!mm_zip_member(zip, slot, &entry.member))
        return "Cannot read a ZIP member record.";
    if (entry.member.directory)
        return NULL;
    length = mm_zip_name(zip, slot, NULL, 0);
    if (length <= 1 || length > ZIP_NAME_CAPACITY)
        return "Invalid ZIP member name.";
    if (mm_zip_name(zip, slot, name, length) != length)
        return "Cannot read a ZIP member name.";
    extension = suffix(name, length - 1);
    if (!extension || memchr(extension, '\0', (size_t)(name + length - 1 - extension)))
        return NULL;
    font = font_extension(extension);
    if (font)
        entry.extension = font;
    else if (!mm_vxp_compare_names(extension, ".3mm"))
        entry.extension = ".3mm";
    else if (!mm_vxp_compare_names(extension, ".vmm"))
        entry.extension = ".vmm";
    else
        return NULL;
    if (!safe_name(name, length - 1) || (!font && length > sizeof(((MarcusMovieEntry *)0)->name)))
        return "Unsafe or overlong movie/font name in ZIP.";
    if (font && catalog->fonts.count >= 128)
        return "A ZIP may contain at most 128 bundled font files.";
    if (catalog->movies.count >= 10000)
        return "A ZIP may contain at most 10,000 movie entries.";
    entry.slot = slot;
    entry.name = NULL;
    if (!append(font ? &catalog->fonts : &catalog->movies, &entry, name, length))
        return "Cannot allocate the ZIP member list.";
    return NULL;
}

const char *marcus_zip_catalog_read(MMZip *zip, int (*cancelled)(void *), void *context, MarcusZipCatalog *result)
{
    MarcusZipCatalog catalog = {0};
    MMU32 i, count = mm_zip_count(zip);
    char *name;
    const char *error = NULL;
    if (count > 100000UL)
        return "ZIP archives are limited to 100,000 members.";
    name = (char *)malloc(ZIP_NAME_CAPACITY);
    if (!name)
        return "Cannot allocate a ZIP member name.";
    for (i = 0; i < count && !error; ++i)
        error = cancelled && cancelled(context) ? "Movie preparation cancelled." : read_member(zip, i, name, &catalog);
    free(name);
    if (!error && !catalog.movies.count)
        error = "This ZIP contains no .3MM or .VMM movies.";
    if (error)
        marcus_zip_catalog_clear(&catalog);
    else
        *result = catalog;
    return error;
}

const char *marcus_zip_check_movie(const MarcusZipEntry *entry)
{
    if (entry->member.encrypted)
        return "Encrypted ZIP movie entries are not supported.";
    if (entry->member.bytes.high || !entry->member.bytes.low || entry->member.bytes.low > MOVIE_BYTES)
        return "A ZIP movie must be nonempty and at most 512 MiB.";
    return NULL;
}

int marcus_zip_check_fonts(const MarcusZipList *fonts)
{
    MMU32 i, remaining = FONT_BYTES;
    const MMZipMember *member;
    for (i = 0; i < fonts->count; ++i)
    {
        member = &fonts->items[i].member;
        if (member->encrypted || member->bytes.high || !member->bytes.low ||
            member->bytes.low > MARCUS_ZIP_FONT_FILE_BYTES || member->bytes.low > remaining)
            return 0;
        remaining -= member->bytes.low;
    }
    return 1;
}

int marcus_zip_legacy_unambiguous(const MarcusZipList *list, const MarcusZipEntry *entry)
{
    MMU32 i;
    for (i = 0; i < list->count; ++i)
        if (&list->items[i] != entry && !strcmp(list->items[i].name, entry->name))
            return 0;
    return 1;
}
