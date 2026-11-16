#ifndef MARCUS_STORYBOARD_NATIVE_H
#define MARCUS_STORYBOARD_NATIVE_H
#include "storyboard.h"
#include "soc_core.h"
// Write ordinary BKGD/CAM/MBMP/ZBMP/GLLT/GLCR resources for every playback host.
void MarcusEmbedBoard(PChunkyFile file, ChunkNumber scene, const MarcusBoardResource &board);
#endif
