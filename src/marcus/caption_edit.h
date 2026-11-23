#ifndef MARCUS_CAPTION_EDIT_H
#define MARCUS_CAPTION_EDIT_H
#include "movie_input.h"
#include "caption_catalog.h"

namespace MM { class Preview; }
struct MMPreviewOwnedCaption;

namespace KauaiFonts { class FontCatalog; }
void MarcusReadCaptionFonts(MarcusCaptionList &, const KauaiFonts::FontCatalog *bundled);
void MarcusReadCaptions(const std::filesystem::path &path, const MM::Preview &preview, MarcusCaptionList &list);
void MarcusDraftCaption(const std::filesystem::path &path, const MarcusCaptionList &list, int index,
                        const MarcusCaptionOptions &options, const char *text, MMPreviewOwnedCaption &);
std::shared_ptr<MarcusMovieFile> MarcusWriteCaption(const std::filesystem::path &path,
    const std::shared_ptr<MarcusMovieFile> &origin, const MarcusCaptionList &list, int index,
    const MarcusCaptionOptions *options, const char *text);
#endif
