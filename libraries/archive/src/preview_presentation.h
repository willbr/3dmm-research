#ifndef THREE_MM_PREVIEW_PRESENTATION_H
#define THREE_MM_PREVIEW_PRESENTATION_H
#include "three_mm/preview.h"
#include "soc_core.h"
namespace ThreeMM
{
namespace Internal
{
bool ReadPresentation(PChunkyFile file, ChunkNumber scene, PreviewPresentation &output, bool soundSheet = false);
bool ReadAudio(PChunkyFile file, ChunkTagOrType type, ChunkNumber sound, PreviewAudio &output);
} // namespace Internal
} // namespace ThreeMM
#endif
