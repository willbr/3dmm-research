#include "archive_file.h"
#include "private_archive_host.h"

MarcusArchive MarcusOpenArchive(const std::filesystem::path &path, int mode)
{
    const auto profile = KauaiPrivateArchiveProfile();
    return MarcusArchive(marcus_archive_open(path.u8string().c_str(), mode, &profile), mm_private_archive_close);
}

MMChunkNameValue MarcusArchiveName(String &name, void *wire)
{
    const auto count = name.Cch();
    if (count)
        name.GetData(wire);
    return {wire, count ? static_cast<MMU32>(name.CbData()) : 0U, static_cast<MMU32>(count)};
}
