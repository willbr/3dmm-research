#include <algorithm>
#include <codecvt>
#include <locale>
#include <cstring>
#include "preview_presentation.h"
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
    CaptionHost host = {file, &out};
    auto services = host.Services();
    const int result = mm_preview_caption(number, &services);
    if (host.exception) std::rethrow_exception(host.exception);
    if (result == -1) throw std::bad_alloc();
    if (result > 0) out.chunk = number;
    return result > 0;
}

} // namespace
#include "preview_presentation_host.h"
bool ReadPresentation(PChunkyFile file, ChunkNumber scene, PreviewPresentation &out, bool soundSheet, bool includeCaptions)
{
    PresentationHost host = {file, &out};
    auto services = host.Services();
    const int result = mm_preview_presentation(scene, soundSheet, includeCaptions, &services);
    if (host.exception) std::rethrow_exception(host.exception);
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
} // namespace MM
