#ifndef MARCUS_STORYBOARD_NATIVE_H
#define MARCUS_STORYBOARD_NATIVE_H
#include "storyboard.h"
#include "archive_file.h"
#include "soc_core.h"
// Embed working resources for Marcus helpers. Published backgrounds use VXP content.
void MarcusEmbedBoard(MMPrivateArchive *file, ChunkNumber scene, const MarcusBoardResource &board);
#endif
