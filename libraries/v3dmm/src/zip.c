/* Licensed under the MIT License. */
#include "mm/zip.h"
#include <miniz.h>
#include <stdlib.h>

struct MMZip
{
    MMZipInput input;
    mz_zip_archive archive;
};

static MMZipSize split(mz_uint64 bytes)
{
    MMZipSize size;
    size.low = (MMU32)bytes;
    size.high = (MMU32)(bytes >> 32);
    return size;
}

static size_t read_zip(void *context, mz_uint64 offset, void *bytes, size_t count)
{
    const MMZipInput *input = &((MMZip *)context)->input;
    return input->read(input->context, split(offset), bytes, count) ? count : 0;
}

MMZipResult mm_zip_open(const MMZipInput *input, MMZip **output)
{
    MMZip *zip = (MMZip *)calloc(1, sizeof(*zip));
    mz_uint64 length;
    if (!zip)
        return MM_ZIP_MEMORY;
    zip->input = *input;
    zip->archive.m_pIO_opaque = zip;
    zip->archive.m_pRead = read_zip;
    length = ((mz_uint64)input->length.high << 32) | input->length.low;
    if (!mz_zip_reader_init(&zip->archive, length, 0))
    {
        free(zip);
        return MM_ZIP_INVALID;
    }
    *output = zip;
    return MM_ZIP_OK;
}

void mm_zip_close(MMZip *zip)
{
    if (zip)
    {
        mz_zip_reader_end(&zip->archive);
        free(zip);
    }
}

MMU32 mm_zip_count(MMZip *zip)
{
    return mz_zip_reader_get_num_files(&zip->archive);
}

int mm_zip_member(MMZip *zip, MMU32 slot, MMZipMember *member)
{
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&zip->archive, slot, &stat))
        return 0;
    member->bytes = split(stat.m_uncomp_size);
    member->crc = (MMU32)stat.m_crc32;
    member->directory = stat.m_is_directory;
    member->encrypted = stat.m_is_encrypted;
    member->supported = stat.m_is_supported;
    return 1;
}

size_t mm_zip_name(MMZip *zip, MMU32 slot, char *buffer, size_t capacity)
{
    if (capacity > (size_t)MZ_UINT32_MAX)
        return 0;
    return mz_zip_reader_get_filename(&zip->archive, slot, buffer, (mz_uint)capacity);
}

typedef struct ZipSink
{
    MMZipSink write;
    void *context;
} ZipSink;

static size_t extract_bytes(void *context, mz_uint64 offset, const void *bytes, size_t count)
{
    const ZipSink *sink = (const ZipSink *)context;
    return sink->write(sink->context, split(offset), bytes, count) ? count : 0;
}

int mm_zip_extract(MMZip *zip, MMU32 slot, MMZipSink write, void *context)
{
    ZipSink sink;
    sink.write = write;
    sink.context = context;
    return mz_zip_reader_extract_to_callback(&zip->archive, slot, extract_bytes, &sink, 0);
}

int mm_zip_extract_memory(MMZip *zip, MMU32 slot, void *bytes, size_t capacity)
{
    return mz_zip_reader_extract_to_mem(&zip->archive, slot, bytes, capacity, 0);
}

void mm_zip_output_init(MMZipOutput *out, MMU32 expected, int (*append)(void *, const void *, size_t), void *context)
{
    out->expected = expected;
    out->written = 0;
    out->crc = MZ_CRC32_INIT;
    out->append = append;
    out->context = context;
}

int mm_zip_output_write(void *context, MMZipSize offset, const void *bytes, size_t count)
{
    MMZipOutput *out = (MMZipOutput *)context;
    if (offset.high || offset.low != out->written || count > out->expected - out->written ||
        !out->append(out->context, bytes, count))
        return 0;
    out->crc = (MMU32)mz_crc32(out->crc, (const unsigned char *)bytes, count);
    out->written += (MMU32)count;
    return 1;
}

int mm_zip_output_complete(const MMZipOutput *out, MMU32 expected_crc)
{
    return out->written == out->expected && out->crc == expected_crc;
}
