/***************************************************************************
    Texture map (br_pixelmap wrapper) host services.
***************************************************************************/
#include "bren_core.h"
#include "handle_services.h"
#include "texture_renderer.h"
#include <cstring>
ASSERTNAME
namespace BRender
{
static_assert(sizeof(TextureMapFile) == 20, "texture header layout");

namespace
{
MMTextureServices TextureServices()
{
    MMTextureServices s = {};
    const unsigned short one = 1;
    s.host_big_endian = !*reinterpret_cast<const byte *>(&one);
    s.system = koskCur;
    s.packed_signature = klwSigPackedFile;
    s.unpacked_signature = klwSigUnpackedFile;
    mm_brender_texture_services(&s);
    s.release_aux = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.allocate = [](void *, MMI32 n, int clear, void **out) -> int {
        return FAllocPv(out, n, clear ? fmemClear : fmemNil, mprNormal);
    };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.invalidate = [](void *, void *p, size_t n) { TrashPvCb(p, static_cast<LegacyLong>(n)); };
    s.diagnostic = [](void *, int code) {
        if (code == MM_TEXTURE_ORDER)
            Assert(false, "bad TextureMapFile");
        else if (code == MM_TEXTURE_PACK_SHRINK)
            Assert(false, "HQ shrink failed");
        else
            Assert(false, "upside down bitmap");
    };
    s.payloads = KauaiChunkPayloadServices();
    s.copy_allocator = KauaiCopyAllocator();
    s.encode = [](void *, void **handle) -> int { return vpcodmUtil->FCompressPhq(handle, cfmtNil); };
#ifndef MAC
    s.read_bitmap = [](void *, void *filename, MMTextureBitmap *out) -> int {
        byte *pixels = nullptr;
        PDynamicArray palette = nullptr;
        LegacyLong w, h;
        bool upside;
        if (!FReadBitmap(static_cast<PFilename>(filename), &pixels, &palette, &w, &h, &upside))
            return 0;
        *out = {pixels, palette, w, h, upside};
        return 1;
    };
#else
    s.read_bitmap = [](void *, void *, MMTextureBitmap *) -> int {
        RawRtn();
        return 0;
    };
#endif
    s.palette_count = [](void *, void *p) -> MMI32 { return static_cast<PDynamicArray>(p)->IvMac(); };
    s.palette_colour = [](void *, void *p, MMI32 i, MMTextureColour *out) {
        Color c;
        static_cast<PDynamicArray>(p)->Get(i, &c);
        *out = {c.bRed, c.bGreen, c.bBlue};
    };
    s.create_file = [](void *, void *p) -> void * { return FileObject::PfilCreate(static_cast<PFilename>(p)); };
    s.make_temporary = [](void *, void *p) { static_cast<PFileObject>(p)->SetTemp(); };
    s.report = [](void *, void *p, int error) {
        PZString message = error == MM_TEXTURE_FILE_CREATE   ? PszLit("Couldn't create destination file\n")
                           : error == MM_TEXTURE_FILE_MEMORY ? PszLit("allocation failure\n")
                                                                   : PszLit("writing to destination file failed\n");
        static_cast<PMSNK>(p)->ReportLine(message);
    };
    return s;
}
const MMTextureOwnerHost &TextureHost()
{
    static const auto services = TextureServices();
    static const MMTextureOwnerHost host = {
        &services, KauaiResourceObjectAllocator(), mm_brender_texture_owner_size(),
#ifdef DEBUG
        [](void *memory) { MarkPv(memory); }
#else
        nullptr
#endif
    };
    return host;
}
}

int EngineTexture::Read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    return mm_texture_fetch(block, out, bytes, &TextureHost());
}

PTextureMap EngineTexture::FromFile(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    return mm_texture_read(file, type, number, &TextureHost());
}

PTextureMap EngineTexture::FromStream(const MMReadStream &stream)
{
    return mm_texture_read_stream(&stream, &TextureHost());
}

PTextureMap EngineTexture::Wrap(PBPMP bpmp)
{
    return mm_texture_wrap(bpmp, &TextureHost());
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
#ifdef DEBUG
    Validate(texture);
#endif
    auto s = *TextureHost().services;
    auto payloads = *s.payloads;
    payloads.files = files;
    s.payloads = &payloads;
    MMU32 n;
    std::memcpy(&n, number, sizeof(n));
    const bool ok = mm_texture_write_chunk(mm_texture_owner_resource(texture), &edit, type, &n, &s) != 0;
    std::memcpy(number, &n, sizeof(n));
    return ok;
}

bool EngineTexture::Write(PTextureMap texture, PDataBlock block)
{
    return mm_texture_write(mm_texture_owner_resource(texture), block->CoreBlock(), TextureHost().services) != 0;
}

PTextureMap EngineTexture::FromBitmap(PFilename filename, PDynamicArray palette)
{
    AssertPo(filename, 0);
    return mm_texture_bitmap(filename, palette, &TextureHost());
}

PTextureMap EngineTexture::New(byte *pixels, LegacyLong width, LegacyLong height)
{
    Assert(width <= ksuMax, "bitmap too wide");
    Assert(height <= ksuMax, "bitmap too high");
    return mm_texture_new(pixels, width, height, &TextureHost());
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
                                 TextureHost().services) != 0;
}

#ifdef DEBUG
void EngineTexture::Validate(PTextureMap texture)
{
    Assert(texture, "Bad texture owner");
    if (texture && !texture->texture.imported_owner)
    {
        const auto view = Pixelmap(texture);
        AssertPvCb(view->pixels, LwMul(view->row_bytes, view->height));
    }
}

void EngineTexture::Mark(PTextureMap texture)
{
    if (texture)
    {
        Validate(texture);
        mm_texture_owner_services()->mark_memory(texture);
        const auto &binding = texture->binding;
        if (binding.cache && !binding.attached)
            MarkMemObj(static_cast<PChunkyResourceFile>(binding.cache->owner));
    }
}
#endif
} // namespace BRender
