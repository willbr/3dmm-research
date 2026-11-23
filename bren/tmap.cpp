/***************************************************************************
    Texture map (br_pixelmap wrapper) host services.
***************************************************************************/
#include "bren_core.h"
#include "handle_services.h"
#include "texture_renderer.h"
ASSERTNAME
namespace BRender
{
static_assert(sizeof(TextureMapFile) == 20, "texture header layout");

PTextureMap EngineTexture::FromFile(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    return mm_brender_texture_from_chunk(file, type, number);
}

PTextureMap EngineTexture::FromStream(const MMReadStream &stream)
{
    return mm_brender_texture_from_stream(&stream);
}

PTextureMap EngineTexture::Wrap(PBPMP bpmp)
{
    return mm_brender_texture_wrap(bpmp);
}

PBPMP EngineTexture::Pixelmap(PTextureMap texture)
{
    return static_cast<PBPMP>(mm_brender_texture_view(texture));
}

bool EngineTexture::Write(PTextureMap texture, PChunkyFile file, ChunkTagOrType type, ChunkNumber *number)
{
    AssertPo(file, 0);
    return Write(texture, file->EditIndex(), KauaiBlockFileServices(), type, number);
}

bool EngineTexture::Write(PTextureMap texture, const MMChunkEdit &edit, const MMBlockFileServices *files,
                          ChunkTagOrType type, ChunkNumber *number)
{
    return mm_brender_texture_write_chunk(texture, &edit, files, type, number) != 0;
}

bool EngineTexture::Write(PTextureMap texture, PDataBlock block)
{
    return mm_texture_write(mm_texture_owner_resource(texture), block->CoreBlock(), mm_kauai_texture_host()->services) != 0;
}

PTextureMap EngineTexture::FromBitmap(PFilename filename, PDynamicArray palette)
{
    AssertPo(filename, 0);
    return mm_brender_texture_from_bitmap(filename, palette);
}

PTextureMap EngineTexture::New(byte *pixels, LegacyLong width, LegacyLong height)
{
    Assert(width <= ksuMax, "bitmap too wide");
    Assert(height <= ksuMax, "bitmap too high");
    return mm_texture_new(pixels, width, height, mm_kauai_texture_host());
}

MMI32 EngineTexture::SizeOnFile(PTextureMap texture)
{
    return mm_texture_size(mm_texture_owner_resource(texture));
}

bool EngineTexture::WriteStandalone(PTextureMap texture, PFilename filename, bool compress, PMSNK sink)
{
#ifdef DEBUG
    Validate(texture);
#endif
    AssertPo(filename, ffniFile);
    AssertNilOrPo(sink, 0);
    return mm_texture_standalone(mm_texture_owner_resource(texture), filename, compress, sink,
                                 mm_kauai_texture_host()->services) != 0;
}

#ifdef DEBUG
void EngineTexture::Validate(PTextureMap texture)
{
    mm_kauai_texture_validate(texture);
}

void EngineTexture::Mark(PTextureMap texture)
{
    mm_brender_texture_mark(texture);
}

#endif
} // namespace BRender
