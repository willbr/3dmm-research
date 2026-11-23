#include <codecvt>
#include <locale>
#include <memory>
#include <utility>
#include "preview_presentation.h"
#include "preview_audio_output.h"
#include "engine_movie_graph.h"
#include "mm/caption_edit.h"
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
int CaptionUtf16(const void *bytes, MMU32 size, int encoding, unsigned short *out, MMU32 capacity, MMU32 *count)
{
    *count = static_cast<MMU32>(
        KauaiText::ToUtf16(bytes, size, static_cast<KauaiText::Encoding>(encoding), out, capacity));
    return 1;
}
template <class F> int StoreCaption(std::exception_ptr &exception, F effect)
{
    try { return effect(); }
    catch (...) { exception = std::current_exception(); return -2; }
}
struct CaptionCodec
{
    String font;
    std::string converted;
    std::exception_ptr exception;

    MMPreviewCaptionCodec Services()
    {
        return {this,
            [](void *p, const MMReadStream *stream, MMU32 offset) {
                auto &host = *static_cast<CaptionCodec *>(p);
                return StoreCaption(host.exception, [&] {
                    unsigned char bytes[kcbMaxDataStn];
                    const auto length = stream->length - offset;
                    const bool read = length <= sizeof(bytes) && mm_read_exact(stream, offset, bytes, length);
                    host.font.FSetData(bytes, read ? static_cast<LegacyLong>(length) : 0);
                    return 1;
                });
            },
            [](void *p, const void *wire, MMU32 bytes, MMCaptionFont *out) {
                auto &host = *static_cast<CaptionCodec *>(p);
                return StoreCaption(host.exception, [&] {
                    if (wire)
                    {
                        String property;
                        if (!property.FSetData(const_cast<void *>(wire), bytes))
                            return 0;
                        host.converted = FontName(property);
                    }
                    else
                        host.converted = FontName(host.font);
                    *out = {host.converted.data(), host.converted.size()};
                    return 1;
                });
            },
            [](void *p, const void *bytes, MMU32 size, int encoding, unsigned short *out, MMU32 capacity, MMU32 *count) {
                auto &host = *static_cast<CaptionCodec *>(p);
                return StoreCaption(host.exception, [&] { return CaptionUtf16(bytes, size, encoding, out, capacity, count); });
            },
            [](void *p, const MMMovieTitle *title, MMCaptionFont *out) {
                auto &host = *static_cast<CaptionCodec *>(p);
                return StoreCaption(host.exception, [&] {
                    String font;
                    font.SetRgch(const_cast<achar *>(static_cast<const achar *>(mm_movie_title_text(title))), title->count);
                    host.converted = FontName(font);
                    *out = {host.converted.data(), host.converted.size()};
                    return 1;
                });
            }};
    }
};

PreviewCaption PublicCaption(const MMPreviewOwnedCaption &source)
{
    const auto &d = source.data;
    PreviewCaption result;
    result.firstFrame = d.first;
    result.endFrame = d.end;
    result.left = d.left;
    result.top = d.top;
    result.right = d.right;
    result.bottom = d.bottom;
    result.story = d.story != 0;
    result.fontSize = d.font_size;
    result.background = d.background;
    result.chunk = source.chunk;
    if (source.font.length)
        result.font.assign(source.font.name, source.font.length);
    if (source.length)
        result.text.assign(source.text, source.text + source.length);
    result.properties.reserve(source.property_count);
    for (std::size_t i = 0; i < source.property_count; ++i)
    {
        const auto &p = source.properties[i];
        PreviewTextProperty property{p.position, p.kind, p.value, {}};
        if (p.font.length)
            property.font.assign(p.font.name, p.font.length);
        result.properties.push_back(std::move(property));
    }
    return result;
}

struct PresentationAdapter
{
    const PreviewPresentationOutput &output;
    std::exception_ptr exception;
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
                      const MMPreviewOwnedPresentationOutput &out, bool soundSheet, bool includeCaptions)
{
    static_assert(witNil == 0 && witLim == 4, "Preview pause admission");
    CaptionCodec host;
    const auto codec = host.Services();
    const int result = mm_preview_presentation_owned(&graph, stored, scene, soundSheet, includeCaptions, &codec, &out);
    if (host.exception) std::rethrow_exception(host.exception);
    if (result == -1) throw std::bad_alloc();
    return result > 0;
}

bool ReadPresentation(const MMMovieSessionGraph &graph, MMMovieGraphBytes stored, ChunkNumber scene,
                      const PreviewPresentationOutput &out, bool soundSheet, bool includeCaptions)
{
    PresentationAdapter output{out, {}};
    const MMPreviewOwnedPresentationOutput services{
        &output,
        [](void *p) {
            const auto &out = static_cast<PresentationAdapter *>(p)->output;
            out.reset(out.context);
        },
        [](void *p, MMI32 transition) {
            const auto &out = static_cast<PresentationAdapter *>(p)->output;
            out.transition(out.context, transition);
        },
        [](void *p, MMPreviewOwnedCaption *source) {
            auto &host = *static_cast<PresentationAdapter *>(p);
            return StoreCaption(host.exception, [&] {
                auto caption = PublicCaption(*source);
                return host.output.caption(host.output.context, caption);
            });
        },
        [](void *p, MMI32 frame, MMI32 kind, MMI32 duration) {
            auto &host = *static_cast<PresentationAdapter *>(p);
            return StoreCaption(host.exception, [&] { return host.output.pause(host.output.context, frame, kind, duration); });
        },
        [](void *p, const MMPreviewCue *cue) {
            auto &host = *static_cast<PresentationAdapter *>(p);
            return StoreCaption(host.exception, [&] { return host.output.cue(host.output.context, cue); });
        }};
    const bool result = ReadPresentation(graph, stored, scene, services, soundSheet, includeCaptions);
    if (output.exception) std::rethrow_exception(output.exception);
    return result;
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
                     MMPreviewOwnedCaption &out)
{
    Internal::CaptionCodec host;
    MMPreviewOwnedCaption caption{};
    const std::unique_ptr<MMPreviewOwnedCaption, decltype(&mm_preview_owned_caption_dispose)>
        owner(&caption, mm_preview_owned_caption_dispose);
    const auto codec = host.Services();
    const int result = mm_preview_caption_body_owned(&box, &text, &stream, encoding, &codec, &caption);
    if (host.exception) std::rethrow_exception(host.exception);
    if (result == -1) throw std::bad_alloc();
    if (result <= 0)
        return false;
    out = caption;
    caption = {};
    return true;
}

bool ReadCaptionData(const MMTextBoxState &box, const MMRichTextState &text, const MMReadStream &stream, int encoding,
                     PreviewCaption &out)
{
    MMPreviewOwnedCaption caption{};
    const std::unique_ptr<MMPreviewOwnedCaption, decltype(&mm_preview_owned_caption_dispose)>
        owner(&caption, mm_preview_owned_caption_dispose);
    if (!ReadCaptionData(box, text, stream, encoding, caption))
        return false;
    caption.chunk = out.chunk;
    out = Internal::PublicCaption(caption);
    return true;
}

int DraftCaptionData(const MMCaptionDraftSource &source, const MMCaptionEditInput &value,
                     const MMCaptionEditInput *before, MMPreviewOwnedCaption &out)
{
    Internal::CaptionCodec host;
    const auto codec = host.Services();
    const int status = mm_caption_edit_draft(&source, &value, before, &codec, &out);
    if (host.exception) std::rethrow_exception(host.exception);
    if (status == MM_CAPTION_EDIT_PREVIEW_MEMORY) throw std::bad_alloc();
    return status;
}
} // namespace MM
