/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    mtrl.cpp: BRender material and costume bindings

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "soc_core.h"
#include "mm/template_runtime.h"
#include "handle_services.h"
#include "material_renderer.h"
#include <cstring>
static_assert(sizeof(MaterialOnFile) == 20 && sizeof(CustomMaterialOnFile) == 8 && sizeof(TXXFF) == 28,
              "material wire layouts");
ASSERTNAME
namespace
{
MMMaterialShade shade;

MMMaterialServices MaterialServices()
{
    MMMaterialServices s = {};
    const unsigned short one = 1;
    s.host_big_endian = !*reinterpret_cast<const byte *>(&one);
    s.system = koskCur;
    s.windows_system = koskSbWin;
    s.texture_type = kctgTmap;
    s.transform_type = kctgTxxf;
    // Preserve the historic cache charges for unpacked MTRL and CMTL objects.
    s.material_bytes = sizeof(void *) == 8 ? 48 : 32;
    s.custom_bytes = sizeof(void *) == 8 ? 64 : 44;
#ifdef DEBUG
    if (sizeof(void *) == 4)
    {
        s.material_bytes += 4;
        s.custom_bytes += 4;
    }
#endif
    mm_brender_material_services(&s);
    s.invalidate = [](void *, void *p, size_t n) { TrashPvCb(p, static_cast<LegacyLong>(n)); };
    s.diagnostic = [](void *, int code) {
        switch (code)
        {
        case MM_MATERIAL_BAD_FILE:
            PushErc(ercSocBadFile);
            break;
        case MM_MATERIAL_BAD_CUSTOM_SIZE:
            Bug("bad CustomMaterialOnFile...you may need to update tmpls.chk");
            break;
        case MM_MATERIAL_BAD_CUSTOM_ORDER:
            Assert(false, "bad CustomMaterialOnFile");
            break;
        case MM_MATERIAL_BAD_TRANSFORM_ORDER:
            Assert(false, "bad TXXFF");
            break;
        }
    };
    s.decode = KauaiDecodeBlockHandle;
    s.texture_reader = EngineTexture::Read;
    s.texture_objects = mm_texture_owner_services();
    s.material_reader = EngineMaterial::Read;
    s.material_objects = mm_material_owner_services();
    s.model_reader = EngineModel::Read;
    s.model_objects = mm_model_owner_services();
    s.allocate = [](void *, MMI32 n, void **out) -> int { return FAllocPv(out, n, fmemClear, mprNormal); };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.texture_pixelmap = [](void *, void *texture) -> void * {
        return EngineTexture::Pixelmap(static_cast<PTextureMap>(texture));
    };
    s.write_texture = [](void *, void *p, const MMChunkEdit *edit, const MMBlockFileServices *files,
                         MMU32 type, MMU32 *number) -> int {
        ChunkNumber n;
        std::memcpy(&n, number, sizeof(n));
        const auto ok = EngineTexture::Write(static_cast<PTextureMap>(p), *edit, files, type, &n);
        std::memcpy(number, &n, sizeof(n));
        return ok;
    };
    s.read_bitmap = [](void *, void *f, void *palette) -> void * {
        return EngineTexture::FromBitmap(static_cast<PFilename>(f), static_cast<PDynamicArray>(palette));
    };
    s.wrap_pixelmap = [](void *, void *p) -> void * { return EngineTexture::Wrap(static_cast<PBPMP>(p)); };
    s.input_material = [](void *, const void *p, MMI32 i) -> void * {
        return static_cast<PMaterial_MTRL const *>(p)[i];
    };
    s.material_handle = [](void *, void *p) -> void * { return EngineMaterial::Handle(static_cast<PMaterial_MTRL>(p)); };
    return s;
}
}

const MMMaterialOwnerHost &EngineMaterial::Host()
{
    static const auto services = MaterialServices();
    static const MMMaterialOwnerHost host = {
        &services, &shade, KauaiResourceObjectAllocator(),
#ifdef DEBUG
        [](void *p) { MarkPv(p); },
        [](MMResourceCache *cache) { MarkMemObj(static_cast<PChunkyResourceFile>(cache->owner)); },
        [](void *p) { EngineTexture::Mark(static_cast<PTextureMap>(p)); },
        [](void *p) { EngineModel::Mark(static_cast<PModel>(p)); }
#else
        nullptr, nullptr, nullptr, nullptr
#endif
    };
    return host;
}

void EngineMaterial::ReleaseShadeTable()
{
    mm_material_shade_dispose(&shade, Host().services);
}

bool EngineMaterial::SetShadeTable(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    AssertPo(file, 0);
    return mm_material_shade_set(&shade, EngineTexture::FromFile(file, type, number), Host().services) != 0;
}

bool EngineMaterial::SetShadeTable(const MMReadStream &stream)
{
    return mm_material_shade_set(&shade, EngineTexture::FromStream(stream), Host().services) != 0;
}

bool EngineMaterial::SetShadeTable(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    int read;
    return mm_resource_cache_visit_payload(cache->CoreCache(), type, number, KauaiDecodeBlockHandle, nullptr,
        [](void *, const MMReadStream *stream) -> int { return SetShadeTable(*stream); }, nullptr, &read) == MM_CHUNK_OK && read;
}

PMaterial_MTRL EngineMaterial::New(LegacyLong base, LegacyLong range)
{
    if (base != ivNil)
        AssertIn(base, 0, kbMax);
    if (range != ivNil)
        AssertIn(range, 0, kbMax - base);
    return mm_material_new(base, range, &Host());
}

int EngineMaterial::Read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    return mm_material_fetch(cache, type, number, block, out, bytes, &Host());
}

PMaterial_MTRL EngineMaterial::Fetch(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    return static_cast<PMaterial_MTRL>(mm_resource_cache_fetch_owned_chunk(cache->CoreCache(), type, number, Read,
                                                                          mm_material_owner_services()));
}

PMaterial_MTRL EngineMaterial::FromPix(PFilename file)
{
    AssertPo(file, ffniFile);
    String path;
    file->GetStnPath(&path);
    return mm_material_import(path.Psz(), nullptr, 0, &Host());
}

PMaterial_MTRL EngineMaterial::FromBitmap(PFilename file, PDynamicArray palette)
{
    AssertPo(file, ffniFile);
#ifdef DEBUG
    EngineTexture::Validate(static_cast<PTextureMap>(shade.texture));
#endif
    return mm_material_import(file, palette, 1, &Host());
}

PBMTL EngineMaterial::Handle(PMaterial_MTRL material)
{
    return static_cast<PBMTL>(mm_material_owner_resource(material)->handle);
}

PMaterial_MTRL EngineMaterial::FromHandle(PBMTL handle)
{
    AssertVarMem(handle);
    auto owner = static_cast<PMaterial_MTRL>(mm_brender_material_owner(handle));
    Assert(owner && Handle(owner) == handle, "Bad Material identifier");
    return owner;
}

PTextureMap EngineMaterial::Texture(PMaterial_MTRL material)
{
    return static_cast<PTextureMap>(mm_material_texture(mm_material_owner_resource(material), Host().services));
}

bool EngineMaterial::Write(PMaterial_MTRL material, PChunkyFile file, ChunkTagOrType type, ChunkNumber *number,
                           const MMMaterialDescription *appearance, const MMI32 mapping[6])
{
    AssertPo(file, 0);
    return Write(material, file->WriteIndex(), type, number, appearance, mapping);
}

bool EngineMaterial::Write(PMaterial_MTRL material, const MMArchiveWriter &writer, ChunkTagOrType type,
                           ChunkNumber *number, const MMMaterialDescription *appearance, const MMI32 mapping[6])
{
#ifdef DEBUG
    Validate(material);
#endif
    AssertVarMem(number);
    MMU32 n;
    std::memcpy(&n, number, sizeof(n));
    const auto ok = mm_material_write_appearance(mm_material_owner_resource(material), &writer.edit,
                                                 writer.payloads->files, type, &n, appearance, mapping,
                                                 Host().services);
    std::memcpy(number, &n, sizeof(n));
    return ok != 0;
}

void EngineMaterial::ReadView(PMaterial_MTRL material, MMMaterialAppearance &out)
{
    mm_material_read_view(mm_material_owner_resource(material), &out, Host().services);
}

bool EngineMaterial::WriteView(PMaterial_MTRL material, const MMArchiveWriter &writer, ChunkTagOrType type,
                               ChunkNumber *number, const MMMaterialAppearance &view)
{
    MMU32 n = *number;
    const auto ok = mm_material_write_view(mm_material_owner_resource(material), &writer, type, &n, &view,
                                           Host().services);
    *number = n;
    return ok != 0;
}

bool EngineCostume::HasModels(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    AssertPo(file, 0);
    const auto graph = file->ReadChunkAccess();
    return mm_template_has_models(&graph, type, number) != 0;
}

bool EngineCostume::EqualModels(PChunkyFile file, ChunkNumber a, ChunkNumber b)
{
    AssertPo(file, 0);
    const auto graph = file->ReadChunkAccess();
    return mm_template_equal_models(&graph, a, b) != 0;
}

PCustomMaterial_CMTL EngineCostume::New(LegacyLong set, LegacyLong count, PMaterial_MTRL *materials)
{
    AssertPvCb(materials, LwMul(count, sizeof(*materials)));
    return mm_custom_material_new(set, count, materials, &EngineMaterial::Host());
}

int EngineCostume::Read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    return mm_custom_material_fetch(cache, type, number, block, out, bytes, &EngineMaterial::Host());
}

PCustomMaterial_CMTL EngineCostume::Fetch(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    return static_cast<PCustomMaterial_CMTL>(mm_resource_cache_fetch_owned_chunk(cache->CoreCache(), type, number, Read,
                                                                               mm_material_owner_services()));
}

PBMTL EngineCostume::Handle(PCustomMaterial_CMTL costume, LegacyLong index)
{
    AssertIn(index, 0, Count(costume));
    return static_cast<PBMTL>(mm_custom_material_handle(mm_material_owner_custom(costume), index,
                                                      EngineMaterial::Host().services));
}

PModel EngineCostume::Model(PCustomMaterial_CMTL costume, LegacyLong index)
{
    AssertIn(index, 0, Count(costume));
    return static_cast<PModel>(mm_custom_material_model(mm_material_owner_custom(costume), index));
}

LegacyLong EngineCostume::Set(PCustomMaterial_CMTL costume)
{
    return mm_material_owner_custom(costume)->set;
}

LegacyLong EngineCostume::Count(PCustomMaterial_CMTL costume)
{
    return mm_material_owner_custom(costume)->count;
}

bool EngineCostume::HasModels(PCustomMaterial_CMTL costume)
{
    return mm_custom_material_has_models(mm_material_owner_custom(costume)) != 0;
}

#ifdef DEBUG
void EngineMaterial::Validate(PMaterial_MTRL material)
{
    const bool valid = material && mm_material_owner_kind(material) == MM_MATERIAL;
    Assert(valid, "Expected a material owner");
    if (valid)
        if (auto texture = Texture(material))
        {
            EngineTexture::Validate(texture);
            Assert(shade.texture != nullptr, "Textured material needs a shade table");
        }
}

void EngineMaterial::Mark(MMMaterialOwner *owner)
{
    if (owner)
        mm_material_owner_services()->mark_memory(owner);
}

void EngineMaterial::MarkShadeTable()
{
    EngineTexture::Mark(static_cast<PTextureMap>(shade.texture));
}

void EngineCostume::Validate(PCustomMaterial_CMTL costume)
{
    const bool valid = costume && mm_material_owner_kind(costume) == MM_CUSTOM_MATERIAL;
    Assert(valid, "Expected a costume owner");
    if (!valid)
        return;
    const auto &data = *mm_material_owner_custom(costume);
    AssertPvCb(data.materials, data.count * sizeof(void *));
    AssertPvCb(data.models, data.count * sizeof(void *));
    for (MMI32 i = 0; i < data.count; ++i)
    {
        if (data.materials[i])
            EngineMaterial::Validate(static_cast<PMaterial_MTRL>(data.materials[i]));
        if (data.models[i])
            EngineModel::Validate(static_cast<PModel>(data.models[i]));
    }
}
#endif
