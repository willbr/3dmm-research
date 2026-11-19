#include <algorithm>
#include <codecvt>
#include <locale>
#include <cstring>
#include "preview_presentation.h"
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
bool Caption(PChunkyFile file, ChunkNumber number, PreviewCaption &out)
{
    TextBoxData box;
    ChunkIdentification child;
    RichTextData text;
    if (!box.FRead(file, number, &child) || !text.FRead(file, child.ctg, child.cno) ||
        !ReadCaptionData(box, text, nullptr, out))
        return false;
    out.chunk = number;
    return true;
}

struct PresentationOutput
{
    PChunkyFile file;
    PreviewPresentation &output;
    std::exception_ptr exception;

    template <class F> int Store(F effect)
    {
        try { return effect(); }
        catch (...) { exception = std::current_exception(); return -2; }
    }
};
} // namespace

bool ReadPresentation(PChunkyFile file, ChunkNumber scene, PreviewPresentation &out, bool soundSheet, bool includeCaptions)
{
    static_assert(witNil == 0 && witLim == 4, "Preview pause admission");
    PresentationOutput output{file, out, {}};
    const MMPreviewPresentationServices services{
        &output,
        [](void *p) { static_cast<PresentationOutput *>(p)->output = {}; },
        [](void *p, MMI32 transition) { static_cast<PresentationOutput *>(p)->output.transition = transition; },
        [](void *p, MMU32 number) {
            auto &host = *static_cast<PresentationOutput *>(p);
            return host.Store([&] {
                PreviewCaption caption;
                if (!Caption(host.file, number, caption))
                    return 0;
                host.output.captions.push_back(std::move(caption));
                return 1;
            });
        },
        [](void *p, MMI32 frame, MMI32 kind, MMI32 duration) {
            auto &host = *static_cast<PresentationOutput *>(p);
            return host.Store([&] { host.output.pauses.push_back({frame, kind, duration}); return 1; });
        },
        [](void *p, const MMPreviewCue *cue) {
            auto &host = *static_cast<PresentationOutput *>(p);
            return host.Store([&] { host.output.sounds.push_back(PreviewCue(*cue)); return 1; });
        }};
    const auto graph = EngineMovieGraph::FromFile(file);
    const int result = mm_preview_presentation(&graph, scene, soundSheet, includeCaptions, &services);
    if (output.exception) std::rethrow_exception(output.exception);
    if (result == -1) throw std::bad_alloc();
    return result > 0;
}
bool ReadAudio(PChunkyFile file, ChunkTagOrType type, ChunkNumber number, PreviewAudio &out)
{
    AudioHost host = {file, &out};
    auto services = host.Services();
    const int result = mm_preview_audio(type, number, &services);
    if (host.exception) std::rethrow_exception(host.exception);
    return result > 0;
}
} // namespace Internal

bool ReadCaptionData(const TextBoxData &box, RichTextData &text, const std::u16string *words, PreviewCaption &out)
{
    Internal::CaptionHost host{box, text, words, out};
    auto services = host.Services();
    const int result = mm_preview_caption(0, &services);
    if (host.exception) std::rethrow_exception(host.exception);
    if (result == -1) throw std::bad_alloc();
    return result > 0;
}
} // namespace MM
