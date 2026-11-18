/* Licensed under the MIT License. */
#include "mm/vxp_zip.h"
#include <miniz.h>
#include <stdlib.h>

struct MMVxpZip
{
    MMVxpZipInput input;
    mz_zip_archive archive;
};

static MMVxpSize split(mz_uint64 bytes)
{
    MMVxpSize size;
    size.low = (MMU32)bytes;
    size.high = (MMU32)(bytes >> 32);
    return size;
}

static size_t read_zip(void *context, mz_uint64 offset, void *bytes, size_t count)
{
    const MMVxpZipInput *input = &((MMVxpZip *)context)->input;
    return input->read(input->context, split(offset), bytes, count) ? count : 0;
}

MMVxpResult mm_vxp_zip_open(const MMVxpZipInput *input, MMVxpZip **output)
{
    MMVxpZip *zip = (MMVxpZip *)calloc(1, sizeof(*zip));
    mz_uint64 length;
    if (!zip)
        return MM_VXP_MEMORY;
    zip->input = *input;
    zip->archive.m_pIO_opaque = zip;
    zip->archive.m_pRead = read_zip;
    length = ((mz_uint64)input->length.high << 32) | input->length.low;
    if (!mz_zip_reader_init(&zip->archive, length, 0))
    {
        free(zip);
        return MM_VXP_ZIP_OPEN;
    }
    *output = zip;
    return MM_VXP_OK;
}

void mm_vxp_zip_close(MMVxpZip *zip)
{
    if (zip)
    {
        mz_zip_reader_end(&zip->archive);
        free(zip);
    }
}

size_t mm_vxp_zip_member_name(MMVxpZip *zip, MMU32 slot, char *buffer, size_t capacity)
{
    if (capacity > (size_t)MZ_UINT32_MAX)
        return 0;
    return mz_zip_reader_get_filename(&zip->archive, slot, buffer, (mz_uint)capacity);
}

static MMVxpResult member_info(void *context, MMU32 slot, char *name, size_t capacity, MMVxpMemberInfo *info)
{
    MMVxpZip *zip = (MMVxpZip *)context;
    mz_zip_archive_file_stat stat;
    size_t length;
    if (!mz_zip_reader_file_stat(&zip->archive, slot, &stat))
        return MM_VXP_MEMBER_RECORD;
    length = mm_vxp_zip_member_name(zip, slot, NULL, 0);
    if (length <= 1 || length > capacity)
        return MM_VXP_NAME_LENGTH;
    if (mm_vxp_zip_member_name(zip, slot, name, length) != length)
        return MM_VXP_NAME_READ;
    info->bytes = split(stat.m_uncomp_size);
    info->name_length = length - 1;
    info->supported = !stat.m_is_directory && !stat.m_is_encrypted && stat.m_is_supported;
    return MM_VXP_OK;
}

static int configuration(void *context, MMU32 slot, void *bytes, size_t count)
{
    return mz_zip_reader_extract_to_mem(&((MMVxpZip *)context)->archive, slot, bytes, count, 0);
}

MMVxpResult mm_vxp_zip_package(MMVxpZip *zip, const MMVxpLimits *limits, MMVxpPackage *output, MMU32 *failed)
{
    MMVxpReader reader;
    reader.context = zip;
    reader.count = mz_zip_reader_get_num_files(&zip->archive);
    reader.member = member_info;
    reader.configuration = configuration;
    return mm_vxp_read_package(&reader, limits, output, failed);
}

typedef struct ZipSink
{
    MMVxpZipSink write;
    void *context;
} ZipSink;

static size_t extract_bytes(void *context, mz_uint64 offset, const void *bytes, size_t count)
{
    const ZipSink *sink = (const ZipSink *)context;
    (void)offset;
    return sink->write(sink->context, bytes, count) ? count : 0;
}

int mm_vxp_zip_extract(MMVxpZip *zip, MMU32 slot, MMVxpZipSink write, void *context)
{
    ZipSink sink;
    sink.write = write;
    sink.context = context;
    return mz_zip_reader_extract_to_callback(&zip->archive, slot, extract_bytes, &sink, 0);
}

typedef struct ZipOutput
{
    mz_zip_archive archive;
    const char *output, *content, *thumbnails;
} ZipOutput;

static int begin(void *context)
{
    ZipOutput *zip = (ZipOutput *)context;
    return mz_zip_writer_init_file(&zip->archive, zip->output, 0);
}

static int write_cfg(void *context, const char *member, const void *bytes, size_t count)
{
    return mz_zip_writer_add_mem(&((ZipOutput *)context)->archive, member, bytes, count, MZ_BEST_COMPRESSION);
}

static int write_chunky(void *context, const char *member, MMVxpMemberKind kind)
{
    ZipOutput *zip = (ZipOutput *)context;
    const char *path = kind == MM_VXP_CONTENT ? zip->content : zip->thumbnails;
    return mz_zip_writer_add_file(&zip->archive, member, path, NULL, 0, MZ_BEST_COMPRESSION);
}

static int finish(void *context)
{
    return mz_zip_writer_finalize_archive(&((ZipOutput *)context)->archive);
}

static int close_zip(void *context)
{
    return mz_zip_writer_end(&((ZipOutput *)context)->archive);
}

MMVxpResult mm_vxp_zip_write_storyboard(const char *output, const char *content, const char *thumbnails,
                                       MMU32 nonce, MMU32 sequence, char name[MM_VXP_STORYBOARD_NAME_CAPACITY])
{
    ZipOutput zip = {{0}, NULL, NULL, NULL};
    MMVxpStoryboardWriter writer;
    zip.output = output;
    zip.content = content;
    zip.thumbnails = thumbnails;
    writer.context = &zip;
    writer.begin = begin;
    writer.configuration = write_cfg;
    writer.chunky = write_chunky;
    writer.finish = finish;
    writer.close = close_zip;
    return mm_vxp_write_storyboard(&writer, nonce, sequence, name);
}
