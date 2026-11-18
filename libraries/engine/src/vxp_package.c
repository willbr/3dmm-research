/* Licensed under the MIT License, except for the adapted sort below. */
/*
 * Adapted from PhysicsFS 1.1.0's __PHYSFS_sort (physfs.c). This altered
 * version sorts VXP member records so duplicate ZIP lookups match the
 * PhysicsFS 1.0.60 DLL shipped with v3dmm 0.14.
 * https://icculus.org/physfs/downloads/physfs-1.1.0.tar.gz
 *
 * Copyright (c) 2003 Ryan C. Gordon and others.
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ryan C. Gordon <icculus@clutteredmind.org>
 */
#include "mm/vxp_package.h"
#include <stdlib.h>
#include <string.h>

#define ZIP_NAME_CAPACITY 65536UL
#define CFG_LIMIT (1024UL * 1024)

static unsigned char ascii_lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

void mm_vxp_fold_ascii(char *text, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i) text[i] = (char)ascii_lower((unsigned char)text[i]);
}

int mm_vxp_compare_names(const char *a, const char *b)
{
    int difference;
    while (*a && *b)
    {
        difference = (int)ascii_lower((unsigned char)*a++) - ascii_lower((unsigned char)*b++);
        if (difference) return difference;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static MMVxpMemberKind member_kind(const char *name)
{
    const char *extension = strrchr(name, '.');
    if (!extension) return MM_VXP_OTHER;
    if (!mm_vxp_compare_names(extension, ".cfg")) return MM_VXP_CONFIG;
    if (!mm_vxp_compare_names(extension, ".3cn")) return MM_VXP_CONTENT;
    if (!mm_vxp_compare_names(extension, ".3th")) return MM_VXP_THUMBNAILS;
    return MM_VXP_OTHER;
}

const char *mm_vxp_content_extension(MMVxpMemberKind kind)
{
    if (kind == MM_VXP_CONTENT) return ".3cn";
    if (kind == MM_VXP_THUMBNAILS) return ".3th";
    return NULL;
}

static char *decimal(char *out, MMU32 value)
{
    char reverse[10];
    size_t count = 0;
    do
    {
        reverse[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count)
        *out++ = reverse[--count];
    *out = '\0';
    return out;
}

static char *copy_text(char *out, const char *text)
{
    size_t length = strlen(text);
    memcpy(out, text, length + 1);
    return out + length;
}

int mm_vxp_materialize(const MMVxpPackage *package, MMVxpMaterializeMember write, void *context)
{
    size_t i;
    char filename[sizeof("4294967295.3cn")];
    const char *extension;
    for (i = 0; i < package->member_count; ++i)
    {
        extension = mm_vxp_content_extension(package->members[i].kind);
        if (!extension)
            continue;
        copy_text(decimal(filename, package->members[i].index), extension);
        if (!write(context, &package->members[i], filename))
            return 0;
    }
    return 1;
}

#define STORYBOARD_DISPLAY "Marcus Storyboard "
#define STORYBOARD_SHORT "marcus_storyboard_"
#define STORYBOARD_CFG "\r\nAuthor=Marcus\r\nType=Portable\r\nContent=Backgrounds\r\nMarcusStoryboard=1\r\n"
#define MAX_STORYBOARD_SUFFIX "4294967295_4294967295"

MMVxpResult mm_vxp_write_storyboard(const MMVxpStoryboardWriter *writer, MMU32 nonce, MMU32 sequence,
                                   char name[MM_VXP_STORYBOARD_NAME_CAPACITY])
{
    char suffix[sizeof(MAX_STORYBOARD_SUFFIX)];
    char member[sizeof(STORYBOARD_SHORT MAX_STORYBOARD_SUFFIX ".cfg")];
    char cfg[sizeof("Name=" STORYBOARD_DISPLAY MAX_STORYBOARD_SUFFIX STORYBOARD_CFG)];
    char display[MM_VXP_STORYBOARD_NAME_CAPACITY];
    char *end = decimal(suffix, nonce);
    size_t cfg_length;
    int complete, closed;
    *end++ = '_';
    decimal(end, sequence);
    copy_text(copy_text(display, STORYBOARD_DISPLAY), suffix);
    end = copy_text(cfg, "Name=");
    end = copy_text(end, display);
    cfg_length = (size_t)(copy_text(end, STORYBOARD_CFG) - cfg);
    end = copy_text(copy_text(member, STORYBOARD_SHORT), suffix);

    if (!writer->begin(writer->context))
        return MM_VXP_ZIP_WRITE;
    copy_text(end, ".cfg");
    complete = writer->configuration(writer->context, member, cfg, cfg_length);
    if (complete)
    {
        copy_text(end, mm_vxp_content_extension(MM_VXP_CONTENT));
        complete = writer->chunky(writer->context, member, MM_VXP_CONTENT);
    }
    if (complete)
    {
        copy_text(end, mm_vxp_content_extension(MM_VXP_THUMBNAILS));
        complete = writer->chunky(writer->context, member, MM_VXP_THUMBNAILS);
    }
    complete = complete && writer->finish(writer->context);
    closed = writer->close(writer->context);
    if (!complete)
        return MM_VXP_ZIP_WRITE;
    if (!closed)
        return MM_VXP_ZIP_CLOSE;
    copy_text(name, display);
    return MM_VXP_OK;
}

static int greater(MMVxpSize a, MMVxpSize b)
{
    return a.high > b.high || (a.high == b.high && a.low > b.low);
}

static int add_bytes(MMVxpSize *total, MMVxpSize bytes, MMVxpSize limit)
{
    MMVxpSize sum;
    int carry;
    sum.low = total->low + bytes.low;
    sum.high = total->high + bytes.high;
    carry = sum.low < total->low;
    if (sum.high < total->high || (carry && sum.high == (MMU32)0xffffffffUL)) return 0;
    sum.high += (MMU32)carry;
    if (greater(sum, limit)) return 0;
    *total = sum;
    return 1;
}

void mm_vxp_dispose(MMVxpPackage *package)
{
    size_t i;
    for (i = 0; i < package->member_count; ++i) free((void *)package->members[i].name);
    free(package->members);
    free(package->fields);
    free(package->configuration);
    free((void *)package->short_name);
    memset(package, 0, sizeof(*package));
}

static MMVxpResult read_members(const MMVxpReader *reader, const MMVxpLimits *limits,
                               MMVxpPackage *package, MMU32 *failed)
{
    char *name, *copy;
    MMVxpMemberInfo info;
    MMVxpMember *member;
    MMVxpSize total = {0, 0};
    MMVxpResult status = MM_VXP_OK;
    size_t count = (size_t)reader->count;
    MMU32 i;
    if (!reader->count || reader->count > limits->members) return MM_VXP_MEMBER_COUNT;
    if (count != reader->count || count > (size_t)-1 / sizeof(*member)) return MM_VXP_MEMORY;
    package->members = (MMVxpMember *)malloc(count * sizeof(*member));
    name = (char *)malloc(ZIP_NAME_CAPACITY);
    if (!package->members || !name) { free(name); return MM_VXP_MEMORY; }
    for (i = 0; i < reader->count; ++i)
    {
        status = reader->member(reader->context, i, name, ZIP_NAME_CAPACITY, &info);
        if (status != MM_VXP_OK) break;
        if (!info.name_length || info.name_length >= ZIP_NAME_CAPACITY) { status = MM_VXP_NAME_LENGTH; break; }
        name[info.name_length] = '\0';
        if (!info.supported || memchr(name, 0, info.name_length) || !strcmp(name, ".") || !strcmp(name, "..") ||
            strpbrk(name, "/\\:"))
        {
            *failed = i;
            status = MM_VXP_UNSAFE_MEMBER;
            break;
        }
        if (greater(info.bytes, limits->member_bytes) || !add_bytes(&total, info.bytes, limits->expanded_bytes))
        { status = MM_VXP_BUDGET; break; }
        copy = (char *)malloc(info.name_length + 1);
        if (!copy) { status = MM_VXP_MEMORY; break; }
        memcpy(copy, name, info.name_length + 1);
        member = &package->members[package->member_count++];
        member->name = copy;
        member->bytes = info.bytes;
        member->index = i;
        member->kind = member_kind(copy);
    }
    free(name);
    return status;
}

static int folded_order(const void *a, const void *b)
{
    const MMVxpMember *left = *(MMVxpMember *const *)a;
    const MMVxpMember *right = *(MMVxpMember *const *)b;
    int order = mm_vxp_compare_names(left->name, right->name);
    if (order) return order;
    return (left->index > right->index) - (left->index < right->index);
}

static void swap(MMVxpMember **a, MMVxpMember **b)
{
    MMVxpMember *entry = *a;
    *a = *b;
    *b = entry;
}

/* Preserve the original unstable permutation, including its equal-name swaps. */
static void sort_zip_members(MMVxpMember **entries, size_t lo, size_t hi)
{
    size_t i, j, pivot;
    int sorted;
    if (hi - lo <= 4)
    {
        do
        {
            sorted = 1;
            for (i = lo; i < hi; ++i)
                if (strcmp(entries[i]->name, entries[i + 1]->name) > 0)
                {
                    swap(entries + i, entries + i + 1);
                    sorted = 0;
                }
        } while (!sorted);
        return;
    }
    i = lo + (hi - lo) / 2;
    if (strcmp(entries[lo]->name, entries[i]->name) > 0) swap(entries + lo, entries + i);
    if (strcmp(entries[lo]->name, entries[hi]->name) > 0) swap(entries + lo, entries + hi);
    if (strcmp(entries[i]->name, entries[hi]->name) > 0) swap(entries + i, entries + hi);
    j = hi - 1;
    swap(entries + i, entries + j);
    pivot = j;
    i = lo;
    for (;;)
    {
        while (strcmp(entries[++i]->name, entries[pivot]->name) < 0) {}
        while (strcmp(entries[--j]->name, entries[pivot]->name) > 0) {}
        if (j < i) break;
        swap(entries + i, entries + j);
    }
    swap(entries + i, entries + hi - 1);
    sort_zip_members(entries, lo, j);
    sort_zip_members(entries, i + 1, hi);
}

static MMU32 zip_lookup(MMVxpMember *const *entries, size_t count, const char *name)
{
    size_t lo = 0, hi = count, mid;
    int order;
    while (lo < hi)
    {
        /* Lower midpoint of the inclusive PhysicsFS search interval. */
        mid = lo + (hi - lo - 1) / 2;
        order = strcmp(name, entries[mid]->name);
        if (!order) return entries[mid]->index;
        if (order < 0) hi = mid;
        else lo = mid + 1;
    }
    return (MMU32)count;
}

static MMVxpResult select_members(MMVxpPackage *package, MMU32 *failed)
{
    size_t i, next = 0, count = package->member_count;
    MMVxpMember **ordered;
    const char *name;
    unsigned char *selected;
    MMU32 conflict = (MMU32)count, slot;
    if (count > (size_t)-1 / sizeof(*ordered)) return MM_VXP_MEMORY;
    ordered = (MMVxpMember **)malloc(count * sizeof(*ordered));
    if (!ordered) return MM_VXP_MEMORY;
    for (i = 0; i < count; ++i) ordered[i] = package->members + i;
    qsort(ordered, count, sizeof(*ordered), folded_order);
    for (i = 1; i < count; ++i)
        if (ordered[i]->index < conflict && !mm_vxp_compare_names(ordered[i - 1]->name, ordered[i]->name) &&
            strcmp(ordered[i - 1]->name, ordered[i]->name))
            conflict = ordered[i]->index;
    if (conflict < count)
    {
        *failed = conflict;
        free(ordered);
        return MM_VXP_AMBIGUOUS_NAME;
    }
    selected = (unsigned char *)calloc(count, 1);
    if (!selected) { free(ordered); return MM_VXP_MEMORY; }
    for (i = 0; i < count; ++i) ordered[i] = package->members + i;
    sort_zip_members(ordered, 0, count - 1);
    for (i = 0; i < count;)
    {
        name = ordered[i]->name;
        slot = zip_lookup(ordered, count, name);
        if (slot >= count)
        {
            free(ordered);
            free(selected);
            return MM_VXP_MEMBER_RECORD;
        }
        selected[slot] = 1;
        do { ++i; } while (i < count && !strcmp(name, ordered[i]->name));
    }
    free(ordered);
    for (i = 0; i < count; ++i)
        if (selected[i]) package->members[next++] = package->members[i];
        else free((void *)package->members[i].name);
    free(selected);
    package->member_count = next;
    return MM_VXP_OK;
}

static int space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static char *trim(char *value)
{
    char *end;
    while (space(*value)) ++value;
    end = value + strlen(value);
    while (end > value && space(end[-1])) --end;
    *end = '\0';
    return value;
}

static int field_order(const void *a, const void *b)
{
    const MMVxpField *left = (const MMVxpField *)a;
    const MMVxpField *right = (const MMVxpField *)b;
    int order = strcmp(left->key, right->key);
    if (order) return order;
    /* Both values point into one configuration buffer, in source order. */
    return (left->value > right->value) - (left->value < right->value);
}

static MMVxpResult parse_fields(MMVxpPackage *package)
{
    char *line, *next, *equals, *key;
    MMVxpField *fields;
    size_t i, capacity = 0, count = 0;
    for (line = package->configuration; line; line = next)
    {
        next = strchr(line, '\n');
        if (next) *next++ = '\0';
        line = trim(line);
        if (!*line || *line == '#' || *line == ';') continue;
        equals = strchr(line, '=');
        if (!equals) continue;
        *equals++ = '\0';
        key = trim(line);
        mm_vxp_fold_ascii(key, strlen(key));
        if (count == capacity)
        {
            capacity = capacity ? capacity * 2 : 16;
            if (capacity > (size_t)-1 / sizeof(*fields)) return MM_VXP_MEMORY;
            fields = (MMVxpField *)realloc(package->fields, capacity * sizeof(*fields));
            if (!fields) return MM_VXP_MEMORY;
            package->fields = fields;
        }
        package->fields[count].key = key;
        package->fields[count++].value = trim(equals);
    }
    if (!count) return MM_VXP_OK;
    qsort(package->fields, count, sizeof(*package->fields), field_order);
    for (i = 0; i < count; ++i)
        if (i + 1 == count || strcmp(package->fields[i].key, package->fields[i + 1].key))
            package->fields[package->field_count++] = package->fields[i];
    return MM_VXP_OK;
}

static const char *find_field(const MMVxpPackage *package, const char *key)
{
    size_t i;
    for (i = 0; i < package->field_count; ++i)
        if (!strcmp(package->fields[i].key, key)) return package->fields[i].value;
    return NULL;
}

int mm_vxp_is_storyboard_package(const MMVxpPackage *package)
{
    const char *value = find_field(package, "marcusstoryboard");
    return value && !strcmp(value, "1");
}

static int valid_override(const MMVxpPackage *package, const char *key, const char *expected)
{
    const char *value = find_field(package, key);
    return !value || !mm_vxp_compare_names(value, expected);
}

static int matching_root(const char *name, const char *root, size_t length)
{
    size_t i;
    if (strlen(name) != length + 4) return 0;
    for (i = 0; i < length; ++i)
        if (ascii_lower((unsigned char)name[i]) != ascii_lower((unsigned char)root[i]))
            return 0;
    return 1;
}

static MMVxpResult read_configuration(const MMVxpReader *reader, MMVxpPackage *package)
{
    const MMVxpMember *member, *cfg = NULL;
    const char *type;
    char *root;
    size_t i, length, root_length;
    int content = 0, thumbnails = 0;
    MMVxpResult status;
    for (i = 0; i < package->member_count; ++i)
        if (package->members[i].kind == MM_VXP_CONFIG)
        {
            if (cfg) return MM_VXP_MULTIPLE_CONFIGS;
            cfg = &package->members[i];
        }
    if (!cfg) return MM_VXP_NO_CONFIG;
    if (cfg->bytes.high || cfg->bytes.low > CFG_LIMIT) return MM_VXP_CONFIG_SIZE;
    length = (size_t)cfg->bytes.low;
    package->configuration = (char *)malloc(length + 1);
    if (!package->configuration) return MM_VXP_MEMORY;
    if (!reader->configuration(reader->context, cfg->index, package->configuration, length))
        return MM_VXP_CONFIG_READ;
    if (memchr(package->configuration, 0, length)) return MM_VXP_CONFIG_TEXT;
    package->configuration[length] = '\0';
    status = parse_fields(package);
    if (status != MM_VXP_OK) return status;
    package->name = find_field(package, "name");
    package->author = find_field(package, "author");
    if (!package->name || !*package->name || strlen(package->name) > 65535 || !package->author || !*package->author)
        return MM_VXP_CONFIG_IDENTITY;
    type = find_field(package, "type");
    if (!type || mm_vxp_compare_names(type, "portable")) return MM_VXP_CONFIG_TYPE;
    if (!valid_override(package, "mount point", "v3dmm")) return MM_VXP_MOUNT_POINT;
    if (!valid_override(package, "regid", "6")) return MM_VXP_REGID;
    if (!valid_override(package, "regname", "virtual3dmm/v3dmm")) return MM_VXP_REGNAME;
    root_length = strlen(cfg->name) - 4;
    for (i = 0; i < package->member_count; ++i)
    {
        member = &package->members[i];
        if ((member->kind == MM_VXP_CONTENT || member->kind == MM_VXP_THUMBNAILS) &&
            matching_root(member->name, cfg->name, root_length))
        {
            if (member->kind == MM_VXP_CONTENT) content = 1;
            else thumbnails = 1;
        }
    }
    if (!content) return MM_VXP_NO_CONTENT;
    if (!thumbnails) return MM_VXP_NO_THUMBNAILS;
    root = (char *)malloc(root_length + 1);
    if (!root) return MM_VXP_MEMORY;
    memcpy(root, cfg->name, root_length);
    root[root_length] = '\0';
    package->short_name = root;
    return MM_VXP_OK;
}

MMVxpResult mm_vxp_read_package(const MMVxpReader *reader, const MMVxpLimits *limits,
                              MMVxpPackage *result, MMU32 *failed_member)
{
    MMVxpPackage package = {0};
    MMU32 failed = reader->count;
    MMVxpResult status = read_members(reader, limits, &package, &failed);
    if (status == MM_VXP_OK) status = select_members(&package, &failed);
    if (status == MM_VXP_OK) status = read_configuration(reader, &package);
    if (status == MM_VXP_OK) *result = package;
    else mm_vxp_dispose(&package);
    if (failed_member) *failed_member = failed;
    return status;
}

const char *mm_vxp_error(MMVxpResult status)
{
    switch (status)
    {
    case MM_VXP_OK: return "";
    case MM_VXP_MEMORY: return "Cannot allocate VXP package data";
    case MM_VXP_MEMBER_COUNT: return "Too many or no VXP members";
    case MM_VXP_MEMBER_RECORD: return "Invalid VXP member record";
    case MM_VXP_NAME_LENGTH: return "Invalid VXP member name length";
    case MM_VXP_NAME_READ: return "Cannot read VXP member name";
    case MM_VXP_UNSAFE_MEMBER: return "Unsupported, unsafe or ambiguous VXP member";
    case MM_VXP_AMBIGUOUS_NAME: return "Ambiguous VXP filename casing";
    case MM_VXP_BUDGET: return "VXP exceeds its extraction budget";
    case MM_VXP_MULTIPLE_CONFIGS: return "VXP has multiple configurations";
    case MM_VXP_NO_CONFIG: return "VXP has no configuration";
    case MM_VXP_CONFIG_SIZE: return "VXP configuration is too large";
    case MM_VXP_CONFIG_READ: return "Cannot decode VXP configuration";
    case MM_VXP_CONFIG_TEXT: return "Invalid VXP configuration text";
    case MM_VXP_CONFIG_IDENTITY: return "VXP configuration needs Name and Author";
    case MM_VXP_CONFIG_TYPE: return "Only Portable VXP packages are supported";
    case MM_VXP_MOUNT_POINT: return "VXP uses an unsupported portable source override: mount point";
    case MM_VXP_REGID: return "VXP uses an unsupported portable source override: regid";
    case MM_VXP_REGNAME: return "VXP uses an unsupported portable source override: regname";
    case MM_VXP_NO_CONTENT: return "VXP has no matching .3cn";
    case MM_VXP_NO_THUMBNAILS: return "VXP has no matching .3th";
    case MM_VXP_ZIP_OPEN: return "Invalid VXP ZIP archive";
    case MM_VXP_ZIP_WRITE: return "Cannot write storyboard VXP package";
    case MM_VXP_ZIP_CLOSE: return "Cannot close storyboard VXP package";
    }
    return "Invalid VXP package";
}
