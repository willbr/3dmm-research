/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    mtrl.cpp: Material (Material_MTRL) and custom material (CustomMaterial_CMTL) classes

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
RTCLASS(Material_MTRL)
RTCLASS(CustomMaterial_CMTL)
MMMaterialShade Material_MTRL::_shade = {};
namespace
{
struct MaterialContext
{
    String path;
};
} // namespace
MMMaterialServices Material_MTRL::_Services(void *context)
{
    MMMaterialServices s = {};
    s.context = context;
    const unsigned short one = 1;
    s.host_big_endian = !*reinterpret_cast<const byte *>(&one);
    s.system = koskCur;
    s.windows_system = koskSbWin;
    s.texture_type = kctgTmap;
    s.transform_type = kctgTxxf;
    s.material_bytes = sizeof(Material_MTRL);
    s.custom_bytes = sizeof(CustomMaterial_CMTL);
    mm_brender_material_services(&s);
    s.create = [](void *, int kind) -> void * {
        if (kind == MM_MATERIAL)
            return NewObj Material_MTRL;
        return NewObj CustomMaterial_CMTL;
    };
    s.retain = [](void *, void *p) { static_cast<BASE *>(p)->AddRef(); };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.material_state = [](void *, void *p) -> MMMaterialResource * {
        return &static_cast<PMaterial_MTRL>(p)->_material;
    };
    s.custom_state = [](void *, void *p) -> MMCustomMaterial * {
        return &static_cast<PCustomMaterial_CMTL>(p)->_custom;
    };
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
    s.texture_reader = TextureMap::FReadTmap;
    s.material_reader = Material_MTRL::FReadMtrl;
    s.model_reader = EngineModel::Read;
    s.model_objects = mm_model_owner_services();
    s.allocate = [](void *, MMI32 n, void **out) -> int { return FAllocPv(out, n, fmemClear, mprNormal); };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.texture_pixelmap = [](void *, void *object) -> void * {
        auto texture = static_cast<PTextureMap>(object);
        return texture ? texture->Pbpmp() : nullptr;
    };
    s.write_texture = [](void *, void *p, const MMChunkEdit *edit, const MMBlockFileServices *files,
                         MMU32 type, MMU32 *number) -> int {
        ChunkNumber n;
        std::memcpy(&n, number, sizeof(n));
        const auto ok = static_cast<PTextureMap>(p)->FWrite(*edit, files, type, &n);
        std::memcpy(number, &n, sizeof(n));
        return ok;
    };
    s.read_bitmap = [](void *, void *f, void *palette) -> void * {
        return TextureMap::PtmapReadNative(static_cast<PFilename>(f), static_cast<PDynamicArray>(palette));
    };
    s.pixelmap_path = [](void *p, void *file) -> const void * {
        auto &path = static_cast<MaterialContext *>(p)->path;
        static_cast<PFilename>(file)->GetStnPath(&path);
        return path.Psz();
    };
    s.wrap_pixelmap = [](void *, void *p) -> void * { return TextureMap::PtmapNewFromBpmp(static_cast<PBPMP>(p)); };
    s.input_material = [](void *, const void *p, MMI32 i) -> void * {
        return static_cast<PMaterial_MTRL const *>(p)[i];
    };
    s.material_handle = [](void *, void *p) -> void * { return static_cast<PMaterial_MTRL>(p)->Pbmtl(); };
    return s;
}
void Material_MTRL::ReleaseShadeTable()
{
    auto s = _Services();
    mm_material_shade_dispose(&_shade, &s);
}
bool Material_MTRL::FSetShadeTable(PChunkyFile f, ChunkTagOrType t, ChunkNumber n)
{
    AssertPo(f, 0);
    auto s = _Services();
    return mm_material_shade_set(&_shade, TextureMap::PtmapRead(f, t, n), &s) != 0;
}
bool Material_MTRL::FSetShadeTable(const MMReadStream &stream)
{
    auto s = _Services();
    return mm_material_shade_set(&_shade, TextureMap::PtmapRead(stream), &s) != 0;
}
bool Material_MTRL::FSetShadeTable(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    int read;
    return mm_resource_cache_visit_payload(cache->CoreCache(), type, number, KauaiDecodeBlockHandle, nullptr,
        [](void *, const MMReadStream *stream) -> int { return FSetShadeTable(*stream); }, nullptr, &read) == MM_CHUNK_OK && read;
}
PMaterial_MTRL Material_MTRL::PmtrlNew(LegacyLong base, LegacyLong range)
{
    if (base != ivNil)
        AssertIn(base, 0, kbMax);
    if (range != ivNil)
        AssertIn(range, 0, kbMax - base);
    auto s = _Services();
    auto object = static_cast<PMaterial_MTRL>(mm_material_new(base, range, &s));
    AssertNilOrPo(object, 0);
    return object;
}
int Material_MTRL::FReadMtrl(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
    void **out, MMI32 *bytes)
{
    const auto s = _Services();
    const int ok = mm_material_fetch(cache, type, number, block, out, bytes, &_shade, &s);
    if (ok && out)
        AssertPo(static_cast<PBaseCacheableObject>(*out), 0);
    return ok;
}
bool Material_MTRL::_FInit(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    AssertBaseThis(0);
    AssertPo(cache, 0);
    const auto s = _Services();
    return mm_material_read(this, cache->CoreCache(), type, number, nullptr, &_shade, &s) != 0;
}
PMaterial_MTRL Material_MTRL::PmtrlNewFromPix(PFilename file)
{
    AssertPo(file, ffniFile);
    MaterialContext context;
    auto s = _Services(&context);
    auto object = static_cast<PMaterial_MTRL>(mm_material_import(file, nullptr, 0, &_shade, &s));
    AssertNilOrPo(object, 0);
    return object;
}
PMaterial_MTRL Material_MTRL::PmtrlNewFromBmp(PFilename file, PDynamicArray palette)
{
    AssertPo(file, ffniFile);
    AssertPo(static_cast<PTextureMap>(_shade.texture), 0);
    auto s = _Services();
    return static_cast<PMaterial_MTRL>(mm_material_import(file, palette, 1, &_shade, &s));
}
PMaterial_MTRL Material_MTRL::PmtrlFromBmtl(PBMTL material)
{
    AssertVarMem(material);
    auto owner = static_cast<PMaterial_MTRL>(mm_brender_material_owner(material));
    AssertPo(owner, 0);
    return owner;
}
PTextureMap Material_MTRL::Ptmap()
{
    AssertBaseThis(0);
    auto s = _Services();
    return static_cast<PTextureMap>(mm_material_texture(&_material, &s));
}
bool Material_MTRL::FWrite(PChunkyFile file, ChunkTagOrType type, ChunkNumber *number,
                           const MMMaterialDescription *appearance, const MMI32 mapping[6])
{
    AssertPo(file, 0);
    return FWrite(file->WriteIndex(), type, number, appearance, mapping);
}
bool Material_MTRL::FWrite(const MMArchiveWriter &writer, ChunkTagOrType type, ChunkNumber *number,
                           const MMMaterialDescription *appearance, const MMI32 mapping[6])
{
    AssertThis(0);
    AssertVarMem(number);
    auto s = _Services();
    MMU32 n;
    std::memcpy(&n, number, sizeof(n));
    const bool ok = mm_material_write_appearance(&_material, &writer.edit, writer.payloads->files, type, &n,
                                                 appearance, mapping, &s) != 0;
    std::memcpy(number, &n, sizeof(n));
    return ok;
}
void Material_MTRL::GetAppearance(MMMaterialAppearance &out) const
{
    const auto services = _Services();
    mm_material_read_view(&_material, &out, &services);
}

bool Material_MTRL::FWriteView(const MMArchiveWriter &writer, ChunkTagOrType type, ChunkNumber *number,
                              const MMMaterialAppearance &view)
{
    const auto services = _Services();
    MMU32 result = *number;
    const auto ok = mm_material_write_view(&_material, &writer, type, &result, &view, &services);
    *number = result;
    return ok != 0;
}
Material_MTRL::~Material_MTRL()
{
    AssertBaseThis(0);
    auto s = _Services();
    mm_material_dispose(&_material, &s);
}
#ifdef DEBUG
void Material_MTRL::AssertValid(LegacyUlong)
{
    Material_MTRL_PAR::AssertValid(fobjAllocated);
    AssertNilOrPo(Ptmap(), 0);
    Assert(_shade.texture != nullptr, "Why do we have MTRLs but no shade table?");
}
void Material_MTRL::MarkMem()
{
    AssertThis(0);
    Material_MTRL_PAR::MarkMem();
    auto texture = Ptmap();
    if (texture)
        MarkMemObj(texture);
}
void Material_MTRL::MarkShadeTable()
{
    MarkMemObj(static_cast<PTextureMap>(_shade.texture));
}
#endif
bool CustomMaterial_CMTL::FHasModels(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    AssertPo(file, 0);
    const auto graph = file->ReadChunkAccess();
    return mm_template_has_models(&graph, type, number) != 0;
}
bool CustomMaterial_CMTL::FEqualModels(PChunkyFile file, ChunkNumber a, ChunkNumber b)
{
    AssertPo(file, 0);
    const auto graph = file->ReadChunkAccess();
    return mm_template_equal_models(&graph, a, b) != 0;
}
PCustomMaterial_CMTL CustomMaterial_CMTL::PcmtlNew(LegacyLong set, LegacyLong count, PMaterial_MTRL *materials)
{
    AssertPvCb(materials, LwMul(count, sizeof(*materials)));
    auto s = Material_MTRL::_Services();
    auto object = static_cast<PCustomMaterial_CMTL>(mm_custom_material_new(set, count, materials, &s));
    AssertNilOrPo(object, 0);
    return object;
}
int CustomMaterial_CMTL::FReadCmtl(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
    void **out, MMI32 *bytes)
{
    const auto s = Material_MTRL::_Services();
    const int ok = mm_custom_material_fetch(cache, type, number, block, out, bytes, &s);
    if (ok && out)
        AssertPo(static_cast<PBaseCacheableObject>(*out), 0);
    return ok;
}
bool CustomMaterial_CMTL::_FInit(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    AssertBaseThis(0);
    AssertPo(cache, 0);
    const auto s = Material_MTRL::_Services();
    return mm_custom_material_read(this, cache->CoreCache(), type, number, nullptr, &s) != 0;
}
CustomMaterial_CMTL::~CustomMaterial_CMTL()
{
    AssertBaseThis(0);
    auto s = Material_MTRL::_Services();
    mm_custom_material_dispose(&_custom, &s);
}
PBMTL CustomMaterial_CMTL::Pbmtl(LegacyLong i)
{
    AssertThis(0);
    AssertIn(i, 0, _custom.count);
    auto s = Material_MTRL::_Services();
    return static_cast<PBMTL>(mm_custom_material_handle(&_custom, i, &s));
}
PModel CustomMaterial_CMTL::Pmodl(LegacyLong i)
{
    AssertThis(0);
    AssertIn(i, 0, _custom.count);
    return static_cast<PModel>(mm_custom_material_model(&_custom, i));
}
bool CustomMaterial_CMTL::FHasModels()
{
    AssertThis(0);
    return mm_custom_material_has_models(&_custom) != 0;
}
#ifdef DEBUG
void CustomMaterial_CMTL::AssertValid(LegacyUlong)
{
    CustomMaterial_CMTL_PAR::AssertValid(fobjAllocated);
    AssertPvCb(_custom.materials, _custom.count * sizeof(void *));
    AssertPvCb(_custom.models, _custom.count * sizeof(void *));
    for (MMI32 i = 0; i < _custom.count; ++i)
    {
        AssertPo(static_cast<PMaterial_MTRL>(_custom.materials[i]), 0);
        if (_custom.models[i])
            EngineModel::Validate(static_cast<PModel>(_custom.models[i]));
    }
}
void CustomMaterial_CMTL::MarkMem()
{
    AssertThis(0);
    CustomMaterial_CMTL_PAR::MarkMem();
    MarkPv(_custom.materials);
    MarkPv(_custom.models);
    for (MMI32 i = 0; i < _custom.count; ++i)
    {
        MarkMemObj(static_cast<PMaterial_MTRL>(_custom.materials[i]));
        EngineModel::Mark(static_cast<PModel>(_custom.models[i]));
    }
}
#endif
