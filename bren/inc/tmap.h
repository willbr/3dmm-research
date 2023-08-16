/*************************************************************************

    Texture map (TextureMap)

    This manages I/O and caching for BPMPs

*************************************************************************/
#ifndef TMAP_H
#define TMAP_H

namespace BRender {

const ChunkTag kctgTmap = 'TMAP';
const ChunkTag kctgTxxf = 'TXXF';

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
const ulong kbomTmapf = 0x54555000;

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

/****************************************
    The TextureMap class
****************************************/
typedef class TextureMap *PTextureMap;
#define TextureMap_PAR BaseCacheableObject
#define kclsTextureMap 'TMAP'
class TextureMap : public TextureMap_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
  protected:
    BPMP _bpmp;
    bool _fImported; // if fTrue, BRender allocated the pixels
                     // if fFalse, we allocated the pixels
  protected:
    TextureMap(void)
    {
    } // can't instantiate directly; must use PtmapRead
#ifdef NOT_YET_REVIEWED
    void TextureMap::_SortInverseTable(byte *prgb, long cbRgb, BRCLR brclrLo, BRCLR brclrHi);
#endif // NOT_YET_REVIEWED
  public:
    ~TextureMap(void);

    //  REVIEW *****(peted): MBMP's ...Read function just takes a PDataBlock; this
    //  is more like the FRead... function, just without the BaseCacheableObject stuff.  Why
    //  the difference?
    //	Addendum: to enable compiling 'TMAP' chunks, I added an FWrite that does
    //	take just a PDataBlock.  Should this be necessary for PtmapRead in the future,
    //	it's a simple matter of extracting the code in PtmapRead that is needed,
    //	like I did for FWrite.
    static PTextureMap PtmapRead(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    bool FWrite(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber *pcno);

    //	a chunky resource reader for a TextureMap
    static bool FReadTmap(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb);

    //	Given a BPMP (a Brender br_pixelmap), create a TextureMap
    static PTextureMap PtmapNewFromBpmp(BPMP *pbpmp);

    //	Give back the bpmp for this TextureMap
    BPMP *Pbpmp(void)
    {
        return &_bpmp;
    }

    //	Reads a .bmp file.
    static PTextureMap PtmapReadNative(Filename *pfni, PDynamicArray pglclr = pvNil);

    // Writes a standalone TextureMap-chunk file (not a .chk)
    bool FWriteTmapChkFile(PFilename pfniDst, bool fCompress, PMSNK pmsnkErr = pvNil);

    // Creates a TextureMap from the width, height, and an array of bytes
    static PTextureMap PtmapNew(byte *prgbPixels, long dxWidth, long dxHeight);

    // Some useful file methods
    long CbOnFile(void)
    {
        return (size(TextureMapFile) + LwMul(_bpmp.row_bytes, _bpmp.height));
    }
    bool FWrite(PDataBlock pblck);

#ifdef NOT_YET_REVIEWED
    // Useful shade-table type method
    byte *PrgbBuildInverseTable(void);
#endif // NOT_YET_REVIEWED
};

} // end of namespace BRender

#endif // TMAP_H
