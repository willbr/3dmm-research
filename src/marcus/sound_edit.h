#ifndef MARCUS_SOUND_EDIT_H
#define MARCUS_SOUND_EDIT_H
#include "movie_input.h"
#include "mm/preview.h"
std::shared_ptr<MarcusMovieFile> MarcusPrepareSound(const char *path, int style);
std::shared_ptr<MarcusMovieFile> MarcusEditSound(const std::shared_ptr<MarcusMovieFile> &movie,
    unsigned long scene, const MM::PreviewSoundCue *cue, const MarcusSoundOptions &options,
    const char *importPath, bool remove);
#endif
