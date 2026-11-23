/*************************************************************************

    BRender texture resource bindings

    This manages I/O and caching for BPMPs

*************************************************************************/
#ifndef TMAP_H
#define TMAP_H
#include "texture_renderer.h"

namespace BRender
{

const ChunkTagOrType kctgTmap = 'TMAP';
const ChunkTagOrType kctgTxxf = 'TXXF';

// tmap on file
struct TextureMapFile
{
    short bo;
    short osk;
    short cbRow;
    byte type;
    byte grftmap;
    short xpLeft;
    short ypTop;
    short dxp;
    short dyp;
    short xpOrigin;
    short ypOrigin;
    // void *rgb; 		// pixels follow immediately after TextureMapFile
};
const br_uint_32 kbomTmapf = 0x54555000;

/* A TeXture XransForm on File */
typedef struct TextureTransformFile
{
    short bo;  // byte order
    short osk; // OS kind
    BMAT23 bmat23;
} TXXFF, *PTXXFF;
const ByteOrderMask kbomTxxff = 0x5FFF0000;

// REVIEW *****: should TMAPs have shade table chunks under them, or
//   is the shade table a global animal?  Right now it's global.

using PTextureMap = MMTextureOwner *;
namespace EngineTexture
{
// Share the cache key with material and Preview loads.
static constexpr auto &Read = mm_brender_texture_read;
PTextureMap FromFile(PChunkyFile, ChunkTagOrType, ChunkNumber);
PTextureMap FromStream(const MMReadStream &);
PTextureMap FromBitmap(PFilename, PDynamicArray palette = nullptr);
PTextureMap Wrap(PBPMP);
PTextureMap New(byte *pixels, LegacyLong width, LegacyLong height);
PBPMP Pixelmap(PTextureMap);
MMI32 SizeOnFile(PTextureMap);
bool Write(PTextureMap, PChunkyFile, ChunkTagOrType, ChunkNumber *);
bool Write(PTextureMap, const MMChunkEdit &, const MMBlockFileServices *, ChunkTagOrType, ChunkNumber *);
bool Write(PTextureMap, PDataBlock);
bool WriteStandalone(PTextureMap, PFilename, bool compress, PMSNK sink = nullptr);
#ifdef DEBUG
void Validate(PTextureMap);
void Mark(PTextureMap);
#endif
}

} // end of namespace BRender

#endif // TMAP_H
