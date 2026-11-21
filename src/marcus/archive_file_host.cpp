#include "archive_file.h"
#include "kauai_core.h"

MarcusArchive MarcusOpenArchive(const std::filesystem::path &path, int mode)
{
    MMPrivateArchiveProfile profile = {sizeof(achar), koskCur, MacWin(koskUniWin, koskUniMac), 0, 0,
        KauaiEncodeLegacyChunkName,
        [](void *, const void *bytes, size_t count, int encoding, void *out, size_t capacity) -> size_t {
            return CchTranslateRgb(const_cast<void *>(bytes), count, encoding, static_cast<achar *>(out), capacity);
        }};
#ifdef CHUNK_BIG_INDEX
    profile.expanded = 1;
#endif
    Debug(profile.trash_unused = 1;)
    return MarcusArchive(marcus_archive_open(path.u8string().c_str(), mode, &profile), mm_private_archive_close);
}

MMChunkNameValue MarcusArchiveName(String &name, void *wire)
{
    const auto count = name.Cch();
    if (count)
        name.GetData(wire);
    return {wire, count ? static_cast<MMU32>(name.CbData()) : 0U, static_cast<MMU32>(count)};
}
