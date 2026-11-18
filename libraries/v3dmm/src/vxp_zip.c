/* Licensed under the MIT License. */
#include "mm/vxp_zip.h"
#include <miniz.h>

static MMVxpResult member_info(void *context, MMU32 slot, char *name, size_t capacity, MMVxpMemberInfo *info)
{
    MMZip *zip = (MMZip *)context;
    MMZipMember member;
    size_t length;
    if (!mm_zip_member(zip, slot, &member))
        return MM_VXP_MEMBER_RECORD;
    length = mm_zip_name(zip, slot, NULL, 0);
    if (length <= 1 || length > capacity)
        return MM_VXP_NAME_LENGTH;
    if (mm_zip_name(zip, slot, name, length) != length)
        return MM_VXP_NAME_READ;
    info->bytes.low = member.bytes.low;
    info->bytes.high = member.bytes.high;
    info->name_length = length - 1;
    info->supported = !member.directory && !member.encrypted && member.supported;
    return MM_VXP_OK;
}

static int configuration(void *context, MMU32 slot, void *bytes, size_t count)
{
    return mm_zip_extract_memory((MMZip *)context, slot, bytes, count);
}

MMVxpResult mm_vxp_zip_package(MMZip *zip, const MMVxpLimits *limits, MMVxpPackage *output, MMU32 *failed)
{
    MMVxpReader reader;
    reader.context = zip;
    reader.count = mm_zip_count(zip);
    reader.member = member_info;
    reader.configuration = configuration;
    return mm_vxp_read_package(&reader, limits, output, failed);
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
