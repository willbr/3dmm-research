#ifndef MM_NATIVE_CAPTION_H
#define MM_NATIVE_CAPTION_H
#include "mm/text_store.h"
#include <string>

namespace MM
{
struct PreviewCaption;
// Host bridge for read-only preview of a prepared native text box.
bool ReadCaptionData(const MMTextBoxState &, const MMRichTextState &, const std::u16string *words, PreviewCaption &);
}
#endif
