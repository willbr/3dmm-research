#include <algorithm>
#include <codecvt>
#include <locale>
#include <cstring>
#include <utility>
#include "preview_presentation.h"
#include "preview_audio_output.h"
#include "engine_movie_graph.h"
#include "mm/native_caption.h"
#include "mm/preview_media.h"
#include "platform_text.h"
#undef size
#include "preview_cue_view.h"
ASSERTNAME
namespace MM
{
namespace Internal
{
namespace
{
std::string FontName(String &name)
{
    auto encoding = sizeof(achar) == 1 ? KauaiText::Encoding::WindowsAnsi : KauaiText::Encoding::Utf16LE;
    auto count = KauaiText::ToUtf16(name.Psz(), name.Cch() * sizeof(achar), encoding, nullptr, 0);
    std::u16string text(count, 0);
    if (count)
        KauaiText::ToUtf16(name.Psz(), name.Cch() * sizeof(achar), encoding,
                           reinterpret_cast<std::uint16_t *>(&text[0]), count);
    return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().to_bytes(text);
}
#include "preview_media_host.h"
bool Caption(const MMPreviewCaptionArchive &source, ChunkNumber number, PreviewCaption &out)
{
    String font;
    CaptionOutput output{font, out, {}};
    const auto services = output.Services();
    const int result = mm_preview_caption_graph(number, &source, &services);
    if (output.exception) std::rethrow_exception(output.exception);
    if (result == -1) throw std::bad_alloc();
    if (result <= 0)
        return false;
    out.chunk = number;
    return true;
}

struct PresentationReader
{
    MMPreviewCaptionArchive source;
    const PreviewPresentationOutput &output;
    std::exception_ptr exception;

    template <class F> int Store(F effect)
    {
        try { return effect(); }
        catch (...) { exception = std::current_exception(); return -2; }
    }
};
} // namespace

PreviewPresentationOutput PresentationCollector(PreviewPresentation &out)
{
    return {&out,
        [](void *p) { *static_cast<PreviewPresentation *>(p) = {}; },
        [](void *p, MMI32 transition) { static_cast<PreviewPresentation *>(p)->transition = transition; },
        [](void *p, PreviewCaption &caption) {
            static_cast<PreviewPresentation *>(p)->captions.push_back(std::move(caption));
            return 1;
        },
        [](void *p, MMI32 frame, MMI32 kind, MMI32 duration) {
            static_cast<PreviewPresentation *>(p)->pauses.push_back({frame, kind, duration});
            return 1;
        },
        [](void *p, const MMPreviewCue *cue) {
            static_cast<PreviewPresentation *>(p)->sounds.push_back(PreviewCue(*cue));
            return 1;
        }};
}

bool ReadPresentation(const MMMovieSessionGraph &graph, MMMovieGraphBytes stored, ChunkNumber scene,
                      const PreviewPresentationOutput &out, bool soundSheet, bool includeCaptions)
{
    static_assert(witNil == 0 && witLim == 4, "Preview pause admission");
    const MMPreviewCaptionArchive source{&graph, stored,
        [](void *v, const MMReadStream *stream, MMU32 offset) {
            return static_cast<CaptionOutput *>(v)->LoadFont(*stream, offset);
        }};
    PresentationReader output{source, out, {}};
    const MMPreviewPresentationServices services{
        &output,
        [](void *p) {
            const auto &out = static_cast<PresentationReader *>(p)->output;
            out.reset(out.context);
        },
        [](void *p, MMI32 transition) {
            const auto &out = static_cast<PresentationReader *>(p)->output;
            out.transition(out.context, transition);
        },
        [](void *p, MMU32 number) {
            auto &host = *static_cast<PresentationReader *>(p);
            return host.Store([&] {
                PreviewCaption caption;
                if (!Caption(host.source, number, caption))
                    return 0;
                return host.output.caption(host.output.context, caption);
            });
        },
        [](void *p, MMI32 frame, MMI32 kind, MMI32 duration) {
            auto &host = *static_cast<PresentationReader *>(p);
            return host.Store([&] { return host.output.pause(host.output.context, frame, kind, duration); });
        },
        [](void *p, const MMPreviewCue *cue) {
            auto &host = *static_cast<PresentationReader *>(p);
            return host.Store([&] { return host.output.cue(host.output.context, cue); });
        }};
    const int result = mm_preview_presentation(&graph, scene, soundSheet, includeCaptions, &services);
    if (output.exception) std::rethrow_exception(output.exception);
    if (result == -1) throw std::bad_alloc();
    return result > 0;
}
bool ReadPresentation(const MMMovieSessionGraph &graph, MMMovieGraphBytes stored, ChunkNumber scene,
                      PreviewPresentation &out, bool soundSheet, bool includeCaptions)
{
    return ReadPresentation(graph, stored, scene, PresentationCollector(out), soundSheet, includeCaptions);
}
bool ReadPresentation(PChunkyFile file, ChunkNumber scene, PreviewPresentation &out, bool soundSheet, bool includeCaptions)
{
    return ReadPresentation(EngineMovieGraph::FromFile(file), mm_movie_owner_stored, scene, out, soundSheet, includeCaptions);
}
bool ReadAudio(PChunkyFile file, ChunkTagOrType type, ChunkNumber number, PreviewAudio &out)
{
    AudioOutput output{out, {}};
    const auto services = output.Services();
    const auto graph = EngineMovieGraph::FromFile(file);
    const int result = mm_preview_audio(&graph, type, number, &services);
    if (output.exception) std::rethrow_exception(output.exception);
    return result > 0;
}
} // namespace Internal

bool ReadCaptionData(const MMTextBoxState &box, const MMRichTextState &text, const MMReadStream &stream, int encoding,
                     PreviewCaption &out)
{
    String font;
    font.SetRgch(const_cast<achar *>(static_cast<const achar *>(mm_movie_title_text(&text.font))), text.font.count);
    Internal::CaptionOutput output{font, out, {}};
    const auto services = output.Services();
    const int result = mm_preview_caption_body(&box, &text, &stream, encoding, &services);
    if (output.exception) std::rethrow_exception(output.exception);
    if (result == -1) throw std::bad_alloc();
    return result > 0;
}
} // namespace MM
