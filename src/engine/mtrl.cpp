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
#include "texture_renderer.h"
#include <cstring>
static_assert(sizeof(MaterialOnFile) == 20 && sizeof(CustomMaterialOnFile) == 8 && sizeof(TXXFF) == 28,
              "material wire layouts");
ASSERTNAME
const MMMaterialOwnerHost &EngineMaterial::Host()
{
    return *mm_kauai_material_host();
}

void EngineMaterial::ReleaseShadeTable()
{
    mm_brender_material_shade_clear();
}

bool EngineMaterial::SetShadeTable(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    AssertPo(file, 0);
    return mm_brender_material_shade_take(mm_brender_texture_from_chunk(file, type, number)) != 0;
}

bool EngineMaterial::SetShadeTable(const MMReadStream &stream)
{
    return mm_brender_material_shade_stream(&stream) != 0;
}

bool EngineMaterial::SetShadeTable(MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number)
{
    return mm_brender_material_shade_chunk(cache, type, number) != 0;
}

PMaterial_MTRL EngineMaterial::New(LegacyLong base, LegacyLong range)
{
    if (base != ivNil)
        AssertIn(base, 0, kbMax);
    if (range != ivNil)
        AssertIn(range, 0, kbMax - base);
    return mm_brender_material_new(base, range);
}

PMaterial_MTRL EngineMaterial::Fetch(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    return Fetch(cache->CoreCache(), type, number);
}

PMaterial_MTRL EngineMaterial::Fetch(MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number)
{
    return mm_brender_material_fetch(cache, type, number);
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
    mm_kauai_texture_validate(static_cast<MMTextureOwner *>(mm_brender_material_shade()->texture));
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
    mm_brender_material_read_view(material, &out);
}

bool EngineMaterial::WriteView(PMaterial_MTRL material, const MMArchiveWriter &writer, ChunkTagOrType type,
                               ChunkNumber *number, const MMMaterialAppearance &view)
{
    MMU32 n = *number;
    const auto ok = mm_brender_material_write_view(material, &writer, type, &n, &view);
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

PCustomMaterial_CMTL EngineCostume::Fetch(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    return Fetch(cache->CoreCache(), type, number);
}

PCustomMaterial_CMTL EngineCostume::Fetch(MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number)
{
    return mm_brender_costume_fetch(cache, type, number);
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
    mm_kauai_material_validate(material);
}

void EngineMaterial::Mark(MMMaterialOwner *owner)
{
    mm_brender_material_mark(owner);
}

void EngineMaterial::MarkShadeTable()
{
    mm_brender_texture_mark(static_cast<MMTextureOwner *>(mm_brender_material_shade()->texture));
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
