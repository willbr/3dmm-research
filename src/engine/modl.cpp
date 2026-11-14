/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    modl.cpp: Model class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "soc_core.h"
#include <cstring>
ASSERTNAME

RTCLASS(Model)

static_assert(sizeof(br_vertex) == 32, "model vertex layout");
static_assert(sizeof(BrFaceOnFile) == 32, "model face wire layout");

ThreeMMModelServices Model::_Services(void *context)
{
    ThreeMMModelServices s = {};
    s.context = context;
    const unsigned short one = 1;
    s.host_big_endian = !*reinterpret_cast<const unsigned char *>(&one);
    s.system = koskCur;
    s.face_bytes = sizeof(BRF);
    s.vertex_group_bytes = sizeof(br_vertex_group);
    s.face_group_bytes = sizeof(br_face_group);
    s.prepared_flag = BR_MODF_PREPREPARED;
    s.keep_original_flag = BR_MODF_KEEP_ORIGINAL;
    s.prepare_all = BR_MPREP_ALL;
    s.state = [](void *, void *object) -> ThreeMMModelResource * { return &static_cast<PModel>(object)->_model; };
    s.create = [](void *) -> void * { return NewObj Model; };
    s.release = [](void *, void *object) { static_cast<PModel>(object)->Release(); };
    s.unpack = [](void *, void *block) -> int { return static_cast<PDataBlock>(block)->FUnpackData(); };
    s.size = [](void *, void *block, int packed) -> ThreeMMI32 {
        return static_cast<PDataBlock>(block)->Cb(packed != 0);
    };
    s.read = [](void *, void *block, void *out, ThreeMMI32 bytes, ThreeMMI32 offset) -> int {
        return static_cast<PDataBlock>(block)->FReadRgb(out, bytes, offset);
    };
    s.allocate = [](void *, ThreeMMI32 bytes, int clear, void **out) -> int {
        return FAllocPv(out, bytes, clear ? fmemClear : fmemNil, mprNormal);
    };
    s.free_memory = [](void *, void **out) { FreePpv(out); };
    s.invalidate = [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); };
    s.diagnostic = [](void *, int code) {
        if (code == THREE_MM_MODEL_BAD_ORDER)
            Assert(false, "bad Model!");
    };
    s.save_failed = [](void *) { Warn("model save failed."); };
    s.allocate_model = [](void *, ThreeMMI32 vertices, ThreeMMI32 faces) -> void * {
        char identifier[sizeof(PModel) + 1];
        // BrModelAllocate duplicates a C string. Reserve the full owner slot,
        // rather than passing an empty string whose allocation is one byte.
        std::memset(identifier, ' ', sizeof(PModel));
        identifier[sizeof(PModel)] = 0;
        return BrModelAllocate(identifier, vertices, faces);
    };
    s.identify = [](void *, void *handle, void *object) {
        auto model = static_cast<PBMDL>(handle);
        Assert(CchSz(model->identifier) >= size(void *), "no room for model owner");
        auto owner = static_cast<PModel>(object);
        CopyPb(&owner, model->identifier, sizeof(owner));
    };
    s.copy_input = [](void *, void *handle, const void *vertices, const void *faces, ThreeMMI32 nv, ThreeMMI32 nf) {
        auto m = static_cast<PBMDL>(handle);
        CopyPb(const_cast<void *>(vertices), m->vertices, nv * sizeof(BRV));
        CopyPb(const_cast<void *>(faces), m->faces, nf * sizeof(BRF));
    };
    s.add = [](void *, void *handle) { BrModelAdd(static_cast<PBMDL>(handle)); };
    s.remove = [](void *, void *handle) { BrModelRemove(static_cast<PBMDL>(handle)); };
    s.free_model = [](void *, void *handle) { BrModelFree(static_cast<PBMDL>(handle)); };
    s.component = [](void *, void *handle, int kind, ThreeMMI32 bytes) -> int {
        auto m = static_cast<PBMDL>(handle);
        void *memory = nullptr;
        switch (kind)
        {
        case THREE_MM_MODEL_VERTICES:
            memory = m->prepared_vertices = static_cast<BRV *>(BrResAllocate(m, bytes, BR_MEMORY_PREPARED_VERTICES));
            break;
        case THREE_MM_MODEL_FACES:
            memory = m->prepared_faces = static_cast<BRF *>(BrResAllocate(m, bytes, BR_MEMORY_PREPARED_FACES));
            break;
        case THREE_MM_MODEL_VERTEX_GROUP:
            memory = m->vertex_groups = static_cast<br_vertex_group *>(BrResAllocate(m, bytes, BR_MEMORY_GROUPS));
            break;
        case THREE_MM_MODEL_FACE_GROUP:
            memory = m->face_groups = static_cast<br_face_group *>(BrResAllocate(m, bytes, BR_MEMORY_GROUPS));
            break;
        }
        return memory != nullptr;
    };
    s.vertex_data = [](void *, void *handle, int prepared) -> void * {
        auto m = static_cast<PBMDL>(handle);
        return prepared ? m->prepared_vertices : m->vertices;
    };
    s.set_face = [](void *, void *handle, int prepared, ThreeMMI32 index, const ThreeMMModelFace *f) {
        auto m = static_cast<PBMDL>(handle);
        auto &out = (prepared ? m->prepared_faces : m->faces)[index];
        for (int i = 0; i < 3; ++i)
        {
            out.vertices[i] = static_cast<br_uint_16>(f->vertices[i]);
            out.edges[i] = static_cast<br_uint_16>(f->edges[i]);
            out.n.v[i] = static_cast<br_fraction>(f->normal[i]);
        }
        out.material = nullptr;
        out.smoothing = static_cast<br_uint_16>(f->smoothing);
        out.flags = static_cast<byte>(f->flags);
        out._pad0 = static_cast<byte>(f->pad);
        out.d = f->distance;
    };
    s.get_face = [](void *, void *handle, ThreeMMI32 index, ThreeMMModelFace *f) {
        auto &in = static_cast<PBMDL>(handle)->prepared_faces[index];
        for (int i = 0; i < 3; ++i)
        {
            f->vertices[i] = in.vertices[i];
            f->edges[i] = in.edges[i];
            f->normal[i] = in.n.v[i];
        }
        f->smoothing = in.smoothing;
        f->flags = in.flags;
        f->pad = in._pad0;
        f->distance = in.d;
    };
    s.publish_prepared = [](void *, void *handle, const ThreeMMModelDescription *d, unsigned flag, ThreeMMI32 vg,
                            ThreeMMI32 fg) {
        auto m = static_cast<PBMDL>(handle);
        m->flags = static_cast<br_uint_16>(flag);
        m->nprepared_vertices = static_cast<br_uint_16>(d->vertices);
        m->nprepared_faces = static_cast<br_uint_16>(d->faces);
        m->nvertex_groups = static_cast<br_uint_16>(vg);
        m->nface_groups = static_cast<br_uint_16>(fg);
    };
    s.bind_group = [](void *, void *handle, int faces) {
        auto m = static_cast<PBMDL>(handle);
        if (faces)
        {
            m->face_groups->material = nullptr;
            m->face_groups->faces = m->prepared_faces;
            m->face_groups->nfaces = m->nprepared_faces;
        }
        else
        {
            m->vertex_groups->material = nullptr;
            m->vertex_groups->vertices = m->prepared_vertices;
            m->vertex_groups->nvertices = m->nprepared_vertices;
        }
    };
    s.describe = [](void *, void *handle, ThreeMMModelDescription *d) {
        auto m = static_cast<PBMDL>(handle);
        d->vertices = m->nprepared_vertices;
        d->faces = m->nprepared_faces;
        d->radius = m->radius;
        for (int i = 0; i < 3; ++i)
        {
            d->bounds.min[i] = m->bounds.min.v[i];
            d->bounds.max[i] = m->bounds.max.v[i];
            d->pivot[i] = m->pivot.v[i];
        }
    };
    s.geometry = [](void *, void *handle, const ThreeMMModelDescription *d) {
        auto m = static_cast<PBMDL>(handle);
        m->radius = d->radius;
        for (int i = 0; i < 3; ++i)
        {
            m->bounds.min.v[i] = d->bounds.min[i];
            m->bounds.max.v[i] = d->bounds.max[i];
            m->pivot.v[i] = d->pivot[i];
        }
    };
    s.put = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, const void *data, ThreeMMI32 bytes) -> int {
        return static_cast<PChunkyFile>(file)->FPutPv(const_cast<void *>(data), bytes, type, number);
    };
    s.position = [](void *, void *handle, ThreeMMI32 index, ThreeMMI32 out[3]) {
        auto &p = static_cast<PBMDL>(handle)->prepared_vertices[index].p;
        for (int i = 0; i < 3; ++i)
            out[i] = p.v[i];
    };
    s.set_position = [](void *, void *handle, ThreeMMI32 index, const ThreeMMI32 in[3]) {
        auto &p = static_cast<PBMDL>(handle)->prepared_vertices[index].p;
        for (int i = 0; i < 3; ++i)
            p.v[i] = in[i];
    };
    s.path = [](void *context, void *filename) -> const void * {
        auto path = static_cast<PString>(context);
        static_cast<PFilename>(filename)->GetStnPath(path);
        return path->Psz();
    };
    s.load_dat = [](void *, const void *path) -> void * {
        return BrModelLoad(const_cast<char *>(static_cast<const char *>(path)));
    };
    s.or_flags = [](void *, void *handle, unsigned flags) {
        static_cast<PBMDL>(handle)->flags |= static_cast<br_uint_16>(flags);
    };
    s.prepare = [](void *, void *handle, unsigned flags) {
        BrModelPrepare(static_cast<PBMDL>(handle), static_cast<br_uint_16>(flags));
    };
    return s;
}

PModel Model::PmodlNew(LegacyLong nv, BRV *vertices, LegacyLong nf, BRF *faces)
{
    AssertIn(nv, 0, ksuMax);
    AssertIn(nf, 0, ksuMax);
    AssertPvCb(vertices, LwMul(nv, size(BRV)));
    AssertPvCb(faces, LwMul(nf, size(BRF)));
    auto s = _Services();
    return static_cast<PModel>(three_mm_model_new(nv, vertices, nf, faces, &s));
}
bool Model::FReadModl(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number, PDataBlock block,
                      PBaseCacheableObject *out, LegacyLong *bytes)
{
    AssertPo(cache, 0);
    AssertPo(block, 0);
    AssertNilOrVarMem(out);
    AssertVarMem(bytes);
    auto s = _Services();
    return three_mm_model_fetch(block, out, bytes, &s) != 0;
}
bool Model::_FInit(PDataBlock block)
{
    AssertBaseThis(0);
    AssertPo(block, 0);
    auto s = _Services();
    return three_mm_model_read(this, block, &s) != 0;
}
PModel Model::PmodlReadFromDat(Filename *filename)
{
    AssertPo(filename, ffniFile);
    String path;
    auto s = _Services(&path);
    return static_cast<PModel>(three_mm_model_dat(filename, &s));
}
PModel Model::PmodlFromBmdl(PBMDL handle)
{
    AssertVarMem(handle);
    PModel owner;
    CopyPb(handle->identifier, &owner, sizeof(owner));
    AssertPo(owner, 0);
    return owner;
}
Model::~Model(void)
{
    AssertBaseThis(0);
    auto s = _Services();
    three_mm_model_dispose(&_model, &s);
}
bool Model::FWrite(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    AssertThis(0);
    AssertPo(file, 0);
    auto s = _Services();
    return three_mm_model_write(&_model, file, type, number, &s) != 0;
}
void Model::AdjustTdfCharacter(void)
{
    AssertThis(0);
    auto s = _Services();
    three_mm_model_adjust_glyph(&_model, &s);
}

namespace
{
struct ModelLightingContext
{
    BCAM camera = {};
    BLIT light = {};
};
} // namespace
bool Model::_FPrelight(LegacyLong count, BVEC3 *directions)
{
    AssertIn(count, 1, 10);
    AssertPvCb(directions, LwMul(count, size(BVEC3)));
    AssertBaseThis(0);
    ModelLightingContext context;
    ThreeMMModelLightingServices s = {};
    s.context = &context;
    s.create = [](void *p, int kind, const ThreeMMModelLightingSpec *spec) -> void * {
        auto &c = *static_cast<ModelLightingContext *>(p);
        switch (kind)
        {
        case THREE_MM_MODEL_LIGHT_WORLD:
            return BrActorAllocate(BR_ACTOR_NONE, nullptr);
        case THREE_MM_MODEL_LIGHT_CAMERA:
            c.camera = {"bah",
                        BR_CAMERA_PERSPECTIVE,
                        static_cast<br_angle>(spec->fov),
                        spec->hither,
                        spec->yon,
                        spec->aspect,
                        spec->camera_width,
                        spec->camera_height};
            return BrActorAllocate(BR_ACTOR_CAMERA, &c.camera);
        case THREE_MM_MODEL_LIGHT_ACTOR: {
            c.light.colour = spec->colour;
            c.light.type = BR_LIGHT_DIRECT;
            c.light.attenuation_c = spec->attenuation;
            auto actor = BrActorAllocate(BR_ACTOR_LIGHT, nullptr);
            if (actor)
                actor->type_data = &c.light;
            return actor;
        }
        case THREE_MM_MODEL_LIGHT_COLOUR:
            return BrPixelmapAllocate(BR_PMT_INDEX_8, static_cast<br_uint_16>(spec->width),
                                      static_cast<br_uint_16>(spec->height), nullptr, 0);
        case THREE_MM_MODEL_LIGHT_DEPTH:
            return BrPixelmapAllocate(BR_PMT_DEPTH_16, static_cast<br_uint_16>(spec->width),
                                      static_cast<br_uint_16>(spec->height), nullptr, 0);
        case THREE_MM_MODEL_LIGHT_MATERIAL: {
            auto material = BrMaterialAllocate("Prelighting material");
            if (!material)
                return nullptr;
            material->colour = spec->colour;
            material->ka = static_cast<br_ufraction>(spec->ambient);
            material->kd = static_cast<br_ufraction>(spec->diffuse);
            material->ks = static_cast<br_ufraction>(spec->specular);
            material->power = spec->power;
            material->index_base = static_cast<byte>(spec->index_base);
            material->index_range = static_cast<byte>(spec->index_range);
            material->flags = (spec->material_lit ? BR_MATF_LIGHT : 0) | (spec->material_gouraud ? BR_MATF_GOURAUD : 0);
            return material;
        }
        }
        return nullptr;
    };
    s.attach = [](void *, void *world, void *actor) {
        BrActorAdd(static_cast<PBACT>(world), static_cast<PBACT>(actor));
    };
    s.enable = [](void *, void *actor) { BrLightEnable(static_cast<PBACT>(actor)); };
    s.disable = [](void *, void *actor) { BrLightDisable(static_cast<PBACT>(actor)); };
    s.add_material = [](void *, void *material) { BrMaterialAdd(static_cast<PBMTL>(material)); };
    s.remove_material = [](void *, void *material) { BrMaterialRemove(static_cast<PBMTL>(material)); };
    s.begin = [](void *, void *world, void *camera, void *colour, void *depth) {
        BrZbSceneRenderBegin(static_cast<PBACT>(world), static_cast<PBACT>(camera), static_cast<PBPMP>(colour),
                             static_cast<PBPMP>(depth));
    };
    s.light = [](void *, void *model, void *material, void *world) {
        BrSceneModelLight(static_cast<PBMDL>(model), static_cast<PBMTL>(material), static_cast<PBACT>(world), nullptr);
    };
    s.end = [](void *) { BrZbSceneRenderEnd(); };
    s.free = [](void *, int kind, void *object) {
        if (kind == THREE_MM_MODEL_LIGHT_WORLD)
            BrActorFree(static_cast<PBACT>(object));
        else if (kind == THREE_MM_MODEL_LIGHT_MATERIAL)
            BrMaterialFree(static_cast<PBMTL>(object));
        else
            BrPixelmapFree(static_cast<PBPMP>(object));
    };
    s.multiple_lights = [](void *) { Assert(false, "code needs to get smarter..."); };
    return three_mm_model_prelight(&_model, count, &s) != 0;
}
#ifdef DEBUG
void Model::AssertValid(LegacyUlong grf)
{
    Model_PAR::AssertValid(fobjAllocated);
    AssertVarMem(static_cast<PBMDL>(_model.handle));
    PModel owner;
    CopyPb(static_cast<PBMDL>(_model.handle)->identifier, &owner, sizeof(owner));
    Assert(owner == this, "Bad Model identifier");
}
void Model::MarkMem(void)
{
    AssertThis(0);
    Model_PAR::MarkMem();
}
#endif
