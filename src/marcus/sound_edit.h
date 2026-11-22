#ifndef MARCUS_SOUND_EDIT_H
#define MARCUS_SOUND_EDIT_H
#include "movie_input.h"
#include "sound_edit_core.h"
std::shared_ptr<MarcusMovieFile> MarcusPrepareSound(const char *path, int style);
std::vector<MarcusSoundAsset> MarcusReadSoundAssets(const std::filesystem::path &path);
std::shared_ptr<MarcusMovieFile> MarcusEditSound(const std::shared_ptr<MarcusMovieFile> &movie,
    const MarcusSoundEditPlan &plan, const char *importPath, const char *libraryPath, MMU32 libraryChunk);
#endif
