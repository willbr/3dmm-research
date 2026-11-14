/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    mtrl.cpp: Material (Material_MTRL) and custom material (CustomMaterial_CMTL) classes

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "soc_core.h"
#include "three_mm/template_runtime.h"
#include <cstring>
static_assert(sizeof(MaterialOnFile) == 20 && sizeof(CustomMaterialOnFile) == 8 && sizeof(TXXFF) == 28,
              "material wire layouts");
ASSERTNAME
RTCLASS(Material_MTRL)
RTCLASS(CustomMaterial_CMTL)
ThreeMMMaterialShade Material_MTRL::_shade = {};
namespace
{
struct MaterialContext
{
    DataBlock block;
    String path;
};
} // namespace
ThreeMMMaterialServices Material_MTRL::_Services(void *context)
{
    ThreeMMMaterialServices s = {};
    s.context = context;
    const unsigned short one = 1;
    s.host_big_endian = !*reinterpret_cast<const byte *>(&one);
    s.system = koskCur;
    s.windows_system = koskSbWin;
    s.texture_type = kctgTmap;
    s.transform_type = kctgTxxf;
    s.material_type = kctgMtrl;
    s.model_type = kctgBmdl;
    s.material_bytes = sizeof(Material_MTRL);
    s.custom_bytes = sizeof(CustomMaterial_CMTL);
    s.light_flag = BR_MATF_LIGHT;
    s.gouraud_flag = BR_MATF_GOURAUD;
    s.smooth_flag = BR_MATF_SMOOTH;
    s.map_flag = BR_MATF_MAP_COLOUR;
    s.create = [](void *, int kind) -> void * {
        if (kind == THREE_MM_MATERIAL)
            return NewObj Material_MTRL;
        return NewObj CustomMaterial_CMTL;
    };
    s.retain = [](void *, void *p) { static_cast<BASE *>(p)->AddRef(); };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.material_state = [](void *, void *p) -> ThreeMMMaterialResource * {
        return &static_cast<PMaterial_MTRL>(p)->_material;
    };
    s.custom_state = [](void *, void *p) -> ThreeMMCustomMaterial * {
        return &static_cast<PCustomMaterial_CMTL>(p)->_custom;
    };
    s.invalidate = [](void *, void *p, size_t n) { TrashPvCb(p, static_cast<LegacyLong>(n)); };
    s.diagnostic = [](void *, int code) {
        switch (code)
        {
        case THREE_MM_MATERIAL_BAD_FILE:
            PushErc(ercSocBadFile);
            break;
        case THREE_MM_MATERIAL_BAD_CUSTOM_SIZE:
            Bug("bad CustomMaterialOnFile...you may need to update tmpls.chk");
            break;
        case THREE_MM_MATERIAL_BAD_CUSTOM_ORDER:
            Assert(false, "bad CustomMaterialOnFile");
            break;
        case THREE_MM_MATERIAL_BAD_TRANSFORM_ORDER:
            Assert(false, "bad TXXFF");
            break;
        }
    };
    s.file = [](void *, void *p) -> void * { return static_cast<PChunkyResourceFile>(p)->Pcfl(); };
    s.find = [](void *p, void *f, ThreeMMU32 type, ThreeMMU32 number) -> void * {
        auto &b = static_cast<MaterialContext *>(p)->block;
        return static_cast<PChunkyFile>(f)->FFind(type, number, &b) ? &b : nullptr;
    };
    s.unpack = [](void *, void *p) -> int { return static_cast<PDataBlock>(p)->FUnpackData(); };
    s.size = [](void *, void *p) -> ThreeMMI32 { return static_cast<PDataBlock>(p)->Cb(); };
    s.read = [](void *, void *p, void *out, ThreeMMI32 n) -> int {
        return static_cast<PDataBlock>(p)->FReadRgb(out, n, 0);
    };
    s.close_block = [](void *p) { static_cast<MaterialContext *>(p)->block.Free(); };
    s.child = [](void *, void *f, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 id, ThreeMMU32 child_type,
                 ThreeMMMaterialChild *out) -> int {
        ChildChunkIdentification kid;
        if (!static_cast<PChunkyFile>(f)->FGetKidChidCtg(type, number, id, child_type, &kid))
            return 0;
        *out = {kid.cki.ctg, kid.cki.cno, kid.chid};
        return 1;
    };
    s.child_at = [](void *, void *f, ThreeMMU32 type, ThreeMMU32 number, ThreeMMI32 i,
                    ThreeMMMaterialChild *out) -> int {
        ChildChunkIdentification kid;
        if (!static_cast<PChunkyFile>(f)->FGetKid(type, number, i, &kid))
            return 0;
        *out = {kid.cki.ctg, kid.cki.cno, kid.chid};
        return 1;
    };
    s.fetch = [](void *, void *p, const ThreeMMMaterialChild *child, int kind) -> void * {
        PFNRPO reader = kind == THREE_MM_MATERIAL_TEXTURE ? TextureMap::FReadTmap
                        : kind == THREE_MM_MATERIAL_MODEL ? Model::FReadModl
                                                          : Material_MTRL::FReadMtrl;
        return static_cast<PChunkyResourceFile>(p)->PbacoFetch(child->type, child->number, reader);
    };
    s.allocate = [](void *, ThreeMMI32 n, void **out) -> int { return FAllocPv(out, n, fmemClear, mprNormal); };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.allocate_material = [](void *) -> void * {
        char name[sizeof(PMaterial_MTRL) + 1];
        std::memset(name, ' ', sizeof(PMaterial_MTRL));
        name[sizeof(PMaterial_MTRL)] = 0;
        return BrMaterialAllocate(name);
    };
    s.identify = [](void *, void *p, void *object) {
        auto owner = static_cast<PMaterial_MTRL>(object);
        CopyPb(&owner, static_cast<PBMTL>(p)->identifier, sizeof(owner));
    };
    s.describe = [](void *, void *p, ThreeMMMaterialDescription *out) {
        auto b = static_cast<PBMTL>(p);
        *out = {b->colour, b->ka, b->kd, b->ks, b->power, b->index_base, b->index_range, b->opacity, b->flags};
    };
    s.configure = [](void *, void *p, const ThreeMMMaterialDescription *in) {
        auto b = static_cast<PBMTL>(p);
        b->colour = in->colour;
        b->ka = static_cast<br_ufraction>(in->ambient);
        b->kd = static_cast<br_ufraction>(in->diffuse);
        b->ks = static_cast<br_ufraction>(in->specular);
        b->power = in->power;
        b->index_base = static_cast<byte>(in->index_base);
        b->index_range = static_cast<byte>(in->index_range);
        b->opacity = static_cast<byte>(in->opacity);
        b->flags = in->flags;
    };
    s.add_material = [](void *, void *p) { BrMaterialAdd(static_cast<PBMTL>(p)); };
    s.remove_material = [](void *, void *p) { BrMaterialRemove(static_cast<PBMTL>(p)); };
    s.free_material = [](void *, void *p) { BrMaterialFree(static_cast<PBMTL>(p)); };
    s.texture = [](void *, void *p) -> void * {
        auto b = static_cast<PBMTL>(p);
        return b->colour_map ? reinterpret_cast<PTextureMap>(b->colour_map->identifier) : nullptr;
    };
    s.map = [](void *, void *p, void *texture, void *raw, void *shade) {
        auto b = static_cast<PBMTL>(p);
        b->colour_map = raw ? static_cast<PBPMP>(raw) : texture ? static_cast<PTextureMap>(texture)->Pbpmp() : nullptr;
        if (shade)
            b->index_shade = static_cast<PTextureMap>(shade)->Pbpmp();
    };
    s.height = [](void *, void *p) -> ThreeMMI32 {
        AssertPo(static_cast<PTextureMap>(p), 0);
        return static_cast<PTextureMap>(p)->Pbpmp()->height;
    };
    s.transform = [](void *, void *p, const ThreeMMI32 matrix[6]) {
        auto b = static_cast<PBMTL>(p);
        std::memcpy(&b->map_transform, matrix, sizeof(b->map_transform));
    };
    s.add_chunk = [](void *, void *f, ThreeMMU32 type, const void *data, ThreeMMI32 bytes, ThreeMMU32 *number) -> int {
        ChunkNumber n;
        std::memcpy(&n, number, sizeof(n));
        const auto ok = static_cast<PChunkyFile>(f)->FAddPv(const_cast<void *>(data), bytes, type, &n);
        std::memcpy(number, &n, sizeof(n));
        return ok;
    };
    s.write_texture = [](void *, void *p, void *f, ThreeMMU32 type, ThreeMMU32 *number) -> int {
        ChunkNumber n;
        std::memcpy(&n, number, sizeof(n));
        const auto ok = static_cast<PTextureMap>(p)->FWrite(static_cast<PChunkyFile>(f), type, &n);
        std::memcpy(number, &n, sizeof(n));
        return ok;
    };
    s.adopt = [](void *, void *f, ThreeMMU32 type, ThreeMMU32 number, const ThreeMMMaterialChild *c) -> int {
        return static_cast<PChunkyFile>(f)->FAdoptChild(type, number, c->type, c->number, c->id);
    };
    s.delete_chunk = [](void *, void *f, ThreeMMU32 type, ThreeMMU32 number) {
        static_cast<PChunkyFile>(f)->Delete(type, number);
    };
    s.read_texture = [](void *, void *f, ThreeMMU32 type, ThreeMMU32 number) -> void * {
        return TextureMap::PtmapRead(static_cast<PChunkyFile>(f), type, number);
    };
    s.read_bitmap = [](void *, void *f, void *palette) -> void * {
        return TextureMap::PtmapReadNative(static_cast<PFilename>(f), static_cast<PDynamicArray>(palette));
    };
    s.read_pixelmap = [](void *p, void *f) -> void * {
        auto &path = static_cast<MaterialContext *>(p)->path;
        static_cast<PFilename>(f)->GetStnPath(&path);
        return BrPixelmapLoad(path.Psz());
    };
    s.wrap_pixelmap = [](void *, void *p) -> void * { return TextureMap::PtmapNewFromBpmp(static_cast<PBPMP>(p)); };
    s.free_pixelmap = [](void *, void *p) { BrPixelmapFree(static_cast<PBPMP>(p)); };
    s.input_material = [](void *, const void *p, ThreeMMI32 i) -> void * {
        return static_cast<PMaterial_MTRL const *>(p)[i];
    };
    s.material_handle = [](void *, void *p) -> void * { return static_cast<PMaterial_MTRL>(p)->Pbmtl(); };
    return s;
}
void Material_MTRL::ReleaseShadeTable()
{
    auto s = _Services();
    three_mm_material_shade_dispose(&_shade, &s);
}
bool Material_MTRL::FSetShadeTable(PChunkyFile f, ChunkTagOrType t, ChunkNumber n)
{
    AssertPo(f, 0);
    auto s = _Services();
    return three_mm_material_shade_set(&_shade, f, t, n, &s) != 0;
}
PMaterial_MTRL Material_MTRL::PmtrlNew(LegacyLong base, LegacyLong range)
{
    if (base != ivNil)
        AssertIn(base, 0, kbMax);
    if (range != ivNil)
        AssertIn(range, 0, kbMax - base);
    auto s = _Services();
    auto object = static_cast<PMaterial_MTRL>(three_mm_material_new(base, range, &s));
    AssertNilOrPo(object, 0);
    return object;
}
bool Material_MTRL::FReadMtrl(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number, PDataBlock block,
                              PBaseCacheableObject *out, LegacyLong *bytes)
{
    AssertPo(cache, 0);
    AssertPo(block, 0);
    AssertVarMem(bytes);
    AssertNilOrVarMem(out);
    MaterialContext context;
    auto s = _Services(&context);
    bool ok = three_mm_material_fetch(cache, type, number, out, bytes, &_shade, &s) != 0;
    if (ok && out)
        AssertPo(*out, 0);
    return ok;
}
bool Material_MTRL::_FInit(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    AssertBaseThis(0);
    AssertPo(cache, 0);
    MaterialContext context;
    auto s = _Services(&context);
    return three_mm_material_read(this, cache, type, number, &_shade, &s) != 0;
}
PMaterial_MTRL Material_MTRL::PmtrlNewFromPix(PFilename file)
{
    AssertPo(file, ffniFile);
    MaterialContext context;
    auto s = _Services(&context);
    auto object = static_cast<PMaterial_MTRL>(three_mm_material_import(file, nullptr, 0, &_shade, &s));
    AssertNilOrPo(object, 0);
    return object;
}
PMaterial_MTRL Material_MTRL::PmtrlNewFromBmp(PFilename file, PDynamicArray palette)
{
    AssertPo(file, ffniFile);
    AssertPo(static_cast<PTextureMap>(_shade.texture), 0);
    auto s = _Services();
    return static_cast<PMaterial_MTRL>(three_mm_material_import(file, palette, 1, &_shade, &s));
}
PMaterial_MTRL Material_MTRL::PmtrlFromBmtl(PBMTL material)
{
    AssertVarMem(material);
    PMaterial_MTRL owner;
    CopyPb(material->identifier, &owner, sizeof(owner));
    AssertPo(owner, 0);
    return owner;
}
PTextureMap Material_MTRL::Ptmap()
{
    AssertBaseThis(0);
    auto s = _Services();
    return static_cast<PTextureMap>(three_mm_material_texture(&_material, &s));
}
bool Material_MTRL::FWrite(PChunkyFile file, ChunkTagOrType type, ChunkNumber *number)
{
    AssertThis(0);
    AssertPo(file, 0);
    AssertVarMem(number);
    auto s = _Services();
    ThreeMMU32 n;
    std::memcpy(&n, number, sizeof(n));
    bool ok = three_mm_material_write(&_material, file, type, &n, &s) != 0;
    std::memcpy(number, &n, sizeof(n));
    return ok;
}
Material_MTRL::~Material_MTRL()
{
    AssertBaseThis(0);
    auto s = _Services();
    three_mm_material_dispose(&_material, &s);
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
static ThreeMMTemplateCreateServices MaterialGraphServices()
{
    ThreeMMTemplateCreateServices s = {};
    s.child = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 id, ThreeMMU32 child_type,
                 ThreeMMTemplateChild *out) -> int {
        ChildChunkIdentification child;
        if (!static_cast<PChunkyFile>(file)->FGetKidChidCtg(type, number, id, child_type, &child))
            return 0;
        *out = {child.cki.ctg, child.cki.cno, child.chid};
        return 1;
    };
    return s;
}
bool CustomMaterial_CMTL::FHasModels(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    AssertPo(file, 0);
    auto s = MaterialGraphServices();
    return three_mm_template_has_models(file, type, number, &s) != 0;
}
bool CustomMaterial_CMTL::FEqualModels(PChunkyFile file, ChunkNumber a, ChunkNumber b)
{
    AssertPo(file, 0);
    auto s = MaterialGraphServices();
    return three_mm_template_equal_models(file, a, b, &s) != 0;
}
PCustomMaterial_CMTL CustomMaterial_CMTL::PcmtlNew(LegacyLong set, LegacyLong count, PMaterial_MTRL *materials)
{
    AssertPvCb(materials, LwMul(count, sizeof(*materials)));
    auto s = Material_MTRL::_Services();
    auto object = static_cast<PCustomMaterial_CMTL>(three_mm_custom_material_new(set, count, materials, &s));
    AssertNilOrPo(object, 0);
    return object;
}
bool CustomMaterial_CMTL::FReadCmtl(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number,
                                    PDataBlock block, PBaseCacheableObject *out, LegacyLong *bytes)
{
    AssertPo(cache, 0);
    AssertPo(block, 0);
    AssertVarMem(bytes);
    AssertNilOrVarMem(out);
    MaterialContext context;
    auto s = Material_MTRL::_Services(&context);
    bool ok = three_mm_custom_material_fetch(cache, type, number, out, bytes, &s) != 0;
    if (ok && out)
        AssertPo(*out, 0);
    return ok;
}
bool CustomMaterial_CMTL::_FInit(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    AssertBaseThis(0);
    AssertPo(cache, 0);
    MaterialContext context;
    auto s = Material_MTRL::_Services(&context);
    return three_mm_custom_material_read(this, cache, type, number, &s) != 0;
}
CustomMaterial_CMTL::~CustomMaterial_CMTL()
{
    AssertBaseThis(0);
    auto s = Material_MTRL::_Services();
    three_mm_custom_material_dispose(&_custom, &s);
}
PBMTL CustomMaterial_CMTL::Pbmtl(LegacyLong i)
{
    AssertThis(0);
    AssertIn(i, 0, _custom.count);
    auto s = Material_MTRL::_Services();
    return static_cast<PBMTL>(three_mm_custom_material_handle(&_custom, i, &s));
}
PModel CustomMaterial_CMTL::Pmodl(LegacyLong i)
{
    AssertThis(0);
    AssertIn(i, 0, _custom.count);
    return static_cast<PModel>(three_mm_custom_material_model(&_custom, i));
}
bool CustomMaterial_CMTL::FHasModels()
{
    AssertThis(0);
    return three_mm_custom_material_has_models(&_custom) != 0;
}
#ifdef DEBUG
void CustomMaterial_CMTL::AssertValid(LegacyUlong)
{
    CustomMaterial_CMTL_PAR::AssertValid(fobjAllocated);
    AssertPvCb(_custom.materials, _custom.count * sizeof(void *));
    AssertPvCb(_custom.models, _custom.count * sizeof(void *));
    for (ThreeMMI32 i = 0; i < _custom.count; ++i)
    {
        AssertPo(static_cast<PMaterial_MTRL>(_custom.materials[i]), 0);
        AssertNilOrPo(static_cast<PModel>(_custom.models[i]), 0);
    }
}
void CustomMaterial_CMTL::MarkMem()
{
    AssertThis(0);
    CustomMaterial_CMTL_PAR::MarkMem();
    MarkPv(_custom.materials);
    MarkPv(_custom.models);
    for (ThreeMMI32 i = 0; i < _custom.count; ++i)
    {
        MarkMemObj(static_cast<PMaterial_MTRL>(_custom.materials[i]));
        MarkMemObj(static_cast<PModel>(_custom.models[i]));
    }
}
#endif
