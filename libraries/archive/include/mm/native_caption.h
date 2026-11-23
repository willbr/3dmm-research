#ifndef MM_NATIVE_CAPTION_H
#define MM_NATIVE_CAPTION_H
#include "mm/text_store.h"

struct MMPreviewOwnedCaption;
namespace MM
{
struct PreviewCaption;
// Host bridge for read-only preview of a prepared native text box.
bool ReadCaptionData(const MMTextBoxState &, const MMRichTextState &, const MMReadStream &, int encoding, PreviewCaption &);
/* Transfer a complete decoded body into an empty owner. */
bool ReadCaptionData(const MMTextBoxState &, const MMRichTextState &, const MMReadStream &, int encoding,
                     MMPreviewOwnedCaption &);
}
#endif
