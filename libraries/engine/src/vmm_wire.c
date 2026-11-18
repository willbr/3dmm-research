/* Licensed under the MIT License. */
#include "mm/vmm_wire.h"
#include <stdlib.h>
#include <string.h>

static const unsigned char signature[] = {'v', '3', 'd', 'm', 'm', 1};
static const unsigned char start_marker[] = {0xf0, 0x0b};
static const unsigned char end_marker[] = {0xfe, 0xeb};

static MMWide word(MMU32 low)
{
    MMWide result;
    result.low = low;
    result.high = 0;
    return result;
}

static MMVmmResult read_bytes(const MMVmmReader *reader, MMWide *offset, void *bytes, size_t count)
{
    MMWide next = mm_wide_add(*offset, word((MMU32)count));
    if (next.high < 0 || mm_wide_compare(next, reader->length) > 0) return MM_VMM_SHORT_READ;
    if (!reader->read_at(reader->context, *offset, bytes, count)) return MM_VMM_READ_FAILED;
    *offset = next;
    return MM_VMM_OK;
}

static MMU32 decode(const unsigned char *bytes, size_t count)
{
    MMU32 value = 0;
    while (count) value = (value << 8) | bytes[--count];
    return value;
}

static void encode(unsigned char *bytes, size_t count, MMU32 value)
{
    size_t i;
    for (i = 0; i < count; ++i)
    {
        bytes[i] = (unsigned char)value;
        value >>= 8;
    }
}

void mm_vmm_dispose(MMVmmDirectory *directory)
{
    size_t i;
    for (i = 0; i < directory->count; ++i) free((void *)directory->entries[i].name);
    free(directory->entries);
    memset(directory, 0, sizeof(*directory));
}

static MMVmmResult read_entries(const MMVmmReader *reader, MMWide *offset, MMVmmDirectory *directory, MMU32 limit)
{
    unsigned char bytes[6];
    MMWide twice, minimum, available;
    MMU32 count;
    size_t i;
    char *name;
    MMVmmEntry *entry;
    MMVmmResult status;
    status = read_bytes(reader, offset, bytes, 4);
    if (status != MM_VMM_OK) return status;
    count = decode(bytes, 4);
    twice = mm_wide_add(word(count), word(count));
    minimum = mm_wide_add(twice, mm_wide_add(twice, twice));
    available = mm_wide_subtract(reader->length, *offset);
    if (!count || count > limit || mm_wide_compare(minimum, available) > 0) return MM_VMM_DEPENDENCY_COUNT;
    if ((size_t)count != count) return MM_VMM_NO_MEMORY;
    i = (size_t)count;
    if (i > (size_t)-1 / sizeof(*entry)) return MM_VMM_NO_MEMORY;
    directory->entries = (MMVmmEntry *)calloc(i, sizeof(*entry));
    if (!directory->entries) return MM_VMM_NO_MEMORY;
    for (i = 0; i < (size_t)count; ++i)
    {
        entry = &directory->entries[i];
        status = read_bytes(reader, offset, bytes, sizeof(bytes));
        if (status != MM_VMM_OK) return status;
        entry->archive_length = decode(bytes, 4);
        entry->name_length = (size_t)decode(bytes + 4, 2);
        if (!entry->name_length) return MM_VMM_EMPTY_NAME;
        name = (char *)malloc(entry->name_length + 1);
        if (!name) return MM_VMM_NO_MEMORY;
        entry->name = name;
        ++directory->count;
        status = read_bytes(reader, offset, name, entry->name_length);
        if (status != MM_VMM_OK) return status;
        if (memchr(name, 0, entry->name_length)) return MM_VMM_INVALID_NAME;
        name[entry->name_length] = '\0';
    }
    return MM_VMM_OK;
}

static MMVmmResult read_archives(const MMVmmReader *reader, MMWide *offset, MMVmmDirectory *directory)
{
    MMVmmEntry *entry;
    unsigned char bytes[2];
    MMWide next;
    size_t i;
    MMVmmResult status;
    for (i = 0; i < directory->count; ++i)
    {
        entry = &directory->entries[i];
        if (!entry->archive_length) continue;
        status = read_bytes(reader, offset, bytes, sizeof(bytes));
        if (status != MM_VMM_OK) return status;
        if (memcmp(bytes, start_marker, sizeof(bytes))) return MM_VMM_START_MARKER;
        next = mm_wide_add(*offset, word(entry->archive_length));
        if (next.high < 0 || mm_wide_compare(next, reader->length) > 0) return MM_VMM_SHORT_ARCHIVE;
        entry->archive_offset = *offset;
        *offset = next;
        status = read_bytes(reader, offset, bytes, sizeof(bytes));
        if (status != MM_VMM_OK) return status;
        if (memcmp(bytes, end_marker, sizeof(bytes))) return MM_VMM_END_MARKER;
    }
    return MM_VMM_OK;
}

MMVmmResult mm_vmm_read(const MMVmmReader *reader, MMU32 max_entries, MMVmmDirectory *result)
{
    MMVmmDirectory directory = {0};
    MMWide offset = {0, 0};
    unsigned char bytes[sizeof(signature)];
    MMVmmResult status;
    status = read_bytes(reader, &offset, bytes, sizeof(signature));
    if (status != MM_VMM_OK) return status;
    if (memcmp(bytes, signature, sizeof(signature))) return MM_VMM_SIGNATURE;
    status = read_entries(reader, &offset, &directory, max_entries);
    if (status == MM_VMM_OK) status = read_archives(reader, &offset, &directory);
    if (status == MM_VMM_OK)
    {
        directory.movie_offset = offset;
        directory.movie_length = mm_wide_subtract(reader->length, offset);
        status = read_bytes(reader, &offset, bytes, 4);
        if (status == MM_VMM_OK && memcmp(bytes, "CHN2", 4)) status = MM_VMM_MOVIE_MAGIC;
    }
    if (status == MM_VMM_OK) *result = directory;
    else mm_vmm_dispose(&directory);
    return status;
}

MMVmmResult mm_vmm_write(const MMVmmWriter *writer, const MMVmmEntry *entries, size_t count)
{
    unsigned char header[6];
    void *context = writer->context;
    const MMVmmEntry *entry;
    size_t i;
    if (!count || count > MM_VMM_OUTPUT_ENTRY_LIMIT) return MM_VMM_OUTPUT_COUNT;
    for (i = 0; i < count; ++i)
    {
        entry = &entries[i];
        if (!entry->name || !entry->name_length || entry->name_length > 65535 ||
            memchr(entry->name, 0, entry->name_length))
            return MM_VMM_OUTPUT_ENTRY;
    }
    encode(header, 4, (MMU32)count);
    if (!writer->begin(context) || !writer->write(context, signature, sizeof(signature)) ||
        !writer->write(context, header, 4)) return MM_VMM_WRITE_FAILED;
    for (i = 0; i < count; ++i)
    {
        entry = &entries[i];
        encode(header, 4, entry->archive_length);
        encode(header + 4, 2, (MMU32)entry->name_length);
        if (!writer->write(context, header, sizeof(header)) ||
            !writer->write(context, entry->name, entry->name_length)) return MM_VMM_WRITE_FAILED;
    }
    for (i = 0; i < count; ++i)
        if (entries[i].archive_length &&
            (!writer->write(context, start_marker, sizeof(start_marker)) || !writer->copy_archive(context, i) ||
             !writer->write(context, end_marker, sizeof(end_marker)))) return MM_VMM_WRITE_FAILED;
    return writer->copy_movie(context) ? MM_VMM_OK : MM_VMM_WRITE_FAILED;
}

const char *mm_vmm_error(MMVmmResult status)
{
    switch (status)
    {
    case MM_VMM_OK: return "";
    case MM_VMM_SHORT_READ: return "Truncated v3dmm data";
    case MM_VMM_READ_FAILED: return "Cannot read v3dmm data";
    case MM_VMM_SIGNATURE: return "Unsupported VMM signature or version";
    case MM_VMM_DEPENDENCY_COUNT: return "Invalid VMM dependency count";
    case MM_VMM_EMPTY_NAME: return "Empty VMM dependency name";
    case MM_VMM_INVALID_NAME: return "Invalid VMM dependency name";
    case MM_VMM_START_MARKER: return "Invalid VMM expansion start marker";
    case MM_VMM_SHORT_ARCHIVE: return "Truncated embedded VXP";
    case MM_VMM_END_MARKER: return "Invalid VMM expansion end marker";
    case MM_VMM_MOVIE_MAGIC: return "VMM contains no chunky movie";
    case MM_VMM_NO_MEMORY: return "Cannot allocate VMM directory";
    case MM_VMM_OUTPUT_COUNT: return "Invalid output dependency count";
    case MM_VMM_OUTPUT_ENTRY: return "VMM output dependency exceeds format limits";
    default: return "Cannot write v3dmm data";
    }
}
