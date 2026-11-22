#include "archive_file.h"
#include "asset_catalog.h"
#include "movie_open.h"
#include "platform_text.h"
#include "private_archive_host.h"

MarcusArchive MarcusOpenArchive(const std::filesystem::path &path, int mode)
{
    const auto profile = KauaiPrivateArchiveProfile();
    return MarcusArchive(marcus_archive_open(path.u8string().c_str(), mode, &profile), mm_private_archive_close);
}

int MarcusLoadAssetCatalog(MarcusAssetCatalog &catalog, MarcusAssetKind kind, const char *directory)
{
    const auto profile = KauaiPrivateArchiveProfile();
    const auto path = (std::filesystem::u8path(directory) / marcus_asset_catalog_file(kind)).u8string();
    return marcus_asset_catalog_load(&catalog, kind, directory, path.c_str(), &profile, KauaiText::Services());
}

const char *MarcusReadMovieOpen(MarcusMovieOpen &movie, MMArchiveReader *reader, MarcusSceneRows &scenes,
                                MMMovieSourceIds &sourceIds, MarcusActorNames &names)
{
    const auto profile = KauaiPrivateArchiveProfile();
    return marcus_movie_open_read(&movie, reader, &scenes, &sourceIds, &profile, KauaiText::Services(), &names);
}

MMChunkNameValue MarcusArchiveName(String &name, void *wire)
{
    const auto count = name.Cch();
    if (count)
        name.GetData(wire);
    return {wire, count ? static_cast<MMU32>(name.CbData()) : 0U, static_cast<MMU32>(count)};
}
