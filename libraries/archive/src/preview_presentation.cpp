#include <algorithm>
#include <codecvt>
#include <locale>
#include <cstring>
#include "preview_presentation.h"
#include "preview_audio_output.h"
#include "engine_movie_graph.h"
#include "mm/native_caption.h"
#include "mm/preview_media.h"
#include "platform_text.h"
#include "mm/text.h"
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

struct PresentationOutput
{
    MMPreviewCaptionArchive source;
    PreviewPresentation &output;
    std::exception_ptr exception;

    template <class F> int Store(F effect)
    {
        try { return effect(); }
        catch (...) { exception = std::current_exception(); return -2; }
    }
};
} // namespace

bool ReadPresentation(const MMMovieSessionGraph &graph, MMMovieGraphBytes stored, ChunkNumber scene,
                      PreviewPresentation &out, bool soundSheet, bool includeCaptions)
{
    static_assert(witNil == 0 && witLim == 4, "Preview pause admission");
    const MMPreviewCaptionArchive source{&graph, stored,
        [](void *v, const MMReadStream *stream, MMU32 offset) {
            return static_cast<CaptionOutput *>(v)->LoadFont(*stream, offset);
        }};
    PresentationOutput output{source, out, {}};
    const MMPreviewPresentationServices services{
        &output,
        [](void *p) { static_cast<PresentationOutput *>(p)->output = {}; },
        [](void *p, MMI32 transition) { static_cast<PresentationOutput *>(p)->output.transition = transition; },
        [](void *p, MMU32 number) {
            auto &host = *static_cast<PresentationOutput *>(p);
            return host.Store([&] {
                PreviewCaption caption;
                if (!Caption(host.source, number, caption))
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
    const int result = mm_preview_presentation(&graph, scene, soundSheet, includeCaptions, &services);
    if (output.exception) std::rethrow_exception(output.exception);
    if (result == -1) throw std::bad_alloc();
    return result > 0;
}
bool ReadPresentation(PChunkyFile file, ChunkNumber scene, PreviewPresentation &out, bool soundSheet, bool includeCaptions)
{
    return ReadPresentation(EngineMovieGraph::FromFile(file), EngineMovieGraph::Stored, scene, out, soundSheet,
                            includeCaptions);
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

bool ReadCaptionData(const MMTextBoxState &box, const MMRichTextState &text, const std::u16string *words,
                     PreviewCaption &out)
{
    String font;
    font.SetRgch(const_cast<achar *>(static_cast<const achar *>(mm_movie_title_text(&text.font))), text.font.count);
    Internal::CaptionOutput output{font, out, {}};
    const auto services = output.Services();
    const unsigned short one = 1;
    const auto nativeEncoding = *reinterpret_cast<const byte *>(&one) ? MM_TEXT_UTF16_LE : MM_TEXT_UTF16_BE;
    struct StoredText
    {
        FileLocation location;
        Internal::CaptionOutput &sink;
    } stored{{static_cast<PFileObject>(text.text.file), static_cast<FilePosition>(text.text.offset), text.text.bytes},
             output};
    MMReadStream stream{&stored, static_cast<MMU32>(text.text.bytes),
        [](void *p, MMU32 offset, void *out, size_t count) -> int {
            auto &source = *static_cast<StoredText *>(p);
            return source.sink.Guard([&] {
                return source.location.FReadRgb(out, static_cast<LegacyLong>(count), offset);
            }) > 0;
        }};
    if (words)
        stream = {const_cast<char16_t *>(words->data()), static_cast<MMU32>(words->size() * 2),
            [](void *p, MMU32 offset, void *out, size_t count) -> int {
                std::memcpy(out, static_cast<unsigned char *>(p) + offset, count);
                return 1;
            }};
    const int result = mm_preview_caption_body(&box, &text, &stream, words ? nativeEncoding : text.text_system, &services);
    if (output.exception) std::rethrow_exception(output.exception);
    if (result == -1) throw std::bad_alloc();
    return result > 0;
}
} // namespace MM
