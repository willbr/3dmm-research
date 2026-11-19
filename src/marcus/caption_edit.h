#ifndef MARCUS_CAPTION_EDIT_H
#define MARCUS_CAPTION_EDIT_H
#include "movie_input.h"
#include "player.h"
#include <cstdint>
#include <string>
#include <vector>

namespace MM { class Preview; struct PreviewCaption; }

struct MarcusCaptionEntry
{
    std::uint32_t slot, chunk;
    MarcusCaptionOptions options;
    std::string text;
};
struct MarcusCaptionList
{
    std::vector<MarcusCaptionEntry> entries;
    std::vector<std::string> fontNames;
    MarcusCaptionOptions defaults{};
    unsigned long document;
    std::uint32_t scene;
    int firstFrame, lastFrame;
};
namespace KauaiFonts { class FontCatalog; }
std::vector<std::string> MarcusCaptionFontNames(const KauaiFonts::FontCatalog *bundled);
void MarcusReadCaptions(const std::filesystem::path &path, const MM::Preview &preview, MarcusCaptionList &list);
MM::PreviewCaption MarcusDraftCaption(const std::filesystem::path &path, const MarcusCaptionList &list, int index,
                                     const MarcusCaptionOptions &options, const char *text);
std::shared_ptr<MarcusMovieFile> MarcusWriteCaption(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusCaptionList &list, int index,
    const MarcusCaptionOptions *options, const char *text);
#endif
