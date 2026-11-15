#include "preview_background_mesh.h"
#include <cstring>
#include <new>
#include "soc_core.h"
#undef size
ASSERTNAME
namespace ThreeMM
{
namespace Internal
{
ThreeMMBackgroundMeshServices PreviewBackgroundMesh::Services()
{
    ThreeMMBackgroundMeshServices s = {};
    s.map_new = [](void *, ThreeMMU32 w, ThreeMMU32 h) -> void * {
        return BrPixelmapAllocate(BR_PMT_INDEX_8, w, h, nullptr, BR_PMAF_NORMAL);
    };
    s.map_row = [](void *, void *object, ThreeMMU32 y) {
        auto p = static_cast<br_pixelmap *>(object);
        return static_cast<unsigned char *>(p->pixels) + y * p->row_bytes;
    };
    s.map_add = [](void *, void *p) { BrMapAdd(static_cast<br_pixelmap *>(p)); };
    s.map_remove = [](void *, void *p) { BrMapRemove(static_cast<br_pixelmap *>(p)); };
    s.map_free = [](void *, void *p) { BrPixelmapFree(static_cast<br_pixelmap *>(p)); };
    s.material_new = [](void *) -> void * { return BrMaterialAllocate(nullptr); };
    s.material_configure = [](void *, void *object, void *map) {
        auto p = static_cast<br_material *>(object);
        p->flags = BR_MATF_TWO_SIDED | BR_MATF_PERSPECTIVE | BR_MATF_MAP_OPAQUE;
        p->colour_map = static_cast<br_pixelmap *>(map);
    };
    s.material_add = [](void *, void *p) { BrMaterialAdd(static_cast<br_material *>(p)); };
    s.material_remove = [](void *, void *p) { BrMaterialRemove(static_cast<br_material *>(p)); };
    s.material_free = [](void *, void *p) { BrMaterialFree(static_cast<br_material *>(p)); };
    s.model_new = [](void *, ThreeMMU32 vertices, ThreeMMU32 faces) -> void * {
        return BrModelAllocate(nullptr, static_cast<br_uint_16>(vertices), static_cast<br_uint_16>(faces));
    };
    s.model_configure = [](void *, void *p) { static_cast<br_model *>(p)->flags = BR_MODF_QUICK_UPDATE; };
    s.vertex = [](void *, void *object, ThreeMMU32 index, const ThreeMMBackgroundVertex *v) {
        auto &p = static_cast<br_model *>(object)->vertices[index];
        p.p.v[0] = BrFloatToScalar(v->x);
        p.p.v[1] = BrFloatToScalar(v->y);
        p.p.v[2] = BrFloatToScalar(v->z);
        p.map.v[0] = BrFloatToScalar(v->u);
        p.map.v[1] = BrFloatToScalar(v->v);
    };
    s.face = [](void *, void *object, ThreeMMU32 index, const ThreeMMU32 indices[3], void *material) {
        auto &p = static_cast<br_model *>(object)->faces[index];
        p.material = static_cast<br_material *>(material);
        for (unsigned i = 0; i < 3; ++i)
            p.vertices[i] = static_cast<br_uint_16>(indices[i]);
    };
    s.model_add = [](void *, void *p) { BrModelAdd(static_cast<br_model *>(p)); };
    s.model_remove = [](void *, void *p) { BrModelRemove(static_cast<br_model *>(p)); };
    s.model_free = [](void *, void *p) { BrModelFree(static_cast<br_model *>(p)); };
    s.actor_new = [](void *) -> void * { return BrActorAllocate(BR_ACTOR_MODEL, nullptr); };
    s.actor_configure = [](void *, void *object, void *model, void *material) {
        auto p = static_cast<br_actor *>(object);
        p->model = static_cast<br_model *>(model);
        p->material = static_cast<br_material *>(material);
        p->t.type = BR_TRANSFORM_IDENTITY;
    };
    s.actor_add = [](void *, void *world, void *actor) {
        BrActorAdd(static_cast<br_actor *>(world), static_cast<br_actor *>(actor));
    };
    return s;
}
PreviewBackgroundMesh::PreviewBackgroundMesh()
{
    if (!three_mm_background_mesh_init(&core, nullptr))
        throw std::bad_alloc();
}
PreviewBackgroundMesh::~PreviewBackgroundMesh()
{
    auto s = Services();
    three_mm_background_mesh_dispose(&core, &s);
}
void PreviewBackgroundMesh::Build(unsigned width, unsigned height, const std::vector<std::uint8_t> &colors,
                                  const std::vector<std::uint16_t> &depths, const BackgroundMatrix &camera,
                                  const BackgroundMatrix &projection)
{
    Build(width, height, colors.data(), colors.size() <= 0xFFFFFFFFU ? static_cast<ThreeMMU32>(colors.size()) : 0,
          depths.data(), depths.size() <= 0xFFFFFFFFU ? static_cast<ThreeMMU32>(depths.size()) : 0, camera, projection);
}
void PreviewBackgroundMesh::Build(unsigned width, unsigned height, const std::uint8_t *colors, std::uint32_t colorCount,
                                  const std::uint16_t *depths, std::uint32_t depthCount, const BackgroundMatrix &camera,
                                  const BackgroundMatrix &projection)
{
    auto s = Services();
    int result = three_mm_background_mesh_build(&core, width, height, colors, colorCount, depths, depthCount,
                                                camera.data(), projection.data(), &s);
    std::memcpy(low.data(), core.low, sizeof(core.low));
    std::memcpy(high.data(), core.high, sizeof(core.high));
    infiniteColours.Reset(core.pixels.buffers[1].data, core.infinite_count);
    if (result < 0)
        throw std::bad_alloc();
}
bool PreviewBackgroundMesh::AddToScene(br_actor *world)
{
    auto s = Services();
    return three_mm_background_mesh_add(&core, world, &s) != 0;
}
} // namespace Internal
} // namespace ThreeMM
