#ifndef MARCUS_CAPTION_EDIT_H
#define MARCUS_CAPTION_EDIT_H
#include "movie_input.h"
#include "player.h"
#include <cstdint>
#include <string>
#include <vector>

namespace MM { class Preview; }

struct MarcusCaptionEntry
{
    std::uint32_t slot, chunk;
    MarcusCaptionOptions options;
    std::string text;
};
struct MarcusCaptionList
{
    std::vector<MarcusCaptionEntry> entries;
    MarcusCaptionOptions defaults{};
    unsigned long document;
    std::uint32_t scene;
    int firstFrame, lastFrame;
};
void MarcusReadCaptions(const std::filesystem::path &path, const MM::Preview &preview, MarcusCaptionList &list);
std::shared_ptr<MarcusMovieFile> MarcusWriteCaption(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusCaptionList &list, int index,
    const MarcusCaptionOptions *options, const char *text);
#endif
