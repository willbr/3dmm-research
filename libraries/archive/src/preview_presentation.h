#ifndef MM_PREVIEW_PRESENTATION_H
#define MM_PREVIEW_PRESENTATION_H
#include "mm/preview.h"
#include "soc_core.h"
namespace MM
{
namespace Internal
{
bool ReadPresentation(PChunkyFile file, ChunkNumber scene, PreviewPresentation &output, bool soundSheet = false,
                      bool includeCaptions = false);
bool ReadAudio(PChunkyFile file, ChunkTagOrType type, ChunkNumber sound, PreviewAudio &output);
} // namespace Internal
} // namespace MM
#endif
