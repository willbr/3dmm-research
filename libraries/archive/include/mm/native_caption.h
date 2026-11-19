#ifndef MM_NATIVE_CAPTION_H
#define MM_NATIVE_CAPTION_H
#include <string>

struct TextBoxData;
class RichTextData;
namespace MM
{
struct PreviewCaption;
// Host bridge for read-only preview of a prepared native text box.
bool ReadCaptionData(const TextBoxData &, RichTextData &, const std::u16string *words, PreviewCaption &);
}
#endif
