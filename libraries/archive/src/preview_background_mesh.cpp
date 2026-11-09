#include "preview_background_mesh.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <cstring>
#include "soc_core.h"
#undef size
ASSERTNAME

namespace ThreeMM
{
namespace Internal
{
namespace
{
bool Continuous(double near, double far)
{
    return std::isfinite(far) && near > 0 && far - near <= std::max(.02, near * .08);
}
} // namespace

struct PreviewBackgroundMesh::Native
{
    br_pixelmap *map = nullptr;
    br_material *material = nullptr;
    std::vector<br_model *> models;
    ~Native()
    {
        for (auto model : models)
            BrModelFree(BrModelRemove(model));
        if (material)
            BrMaterialFree(BrMaterialRemove(material));
        if (map)
            BrPixelmapFree(BrMapRemove(map));
    }
};
PreviewBackgroundMesh::PreviewBackgroundMesh() = default;
PreviewBackgroundMesh::~PreviewBackgroundMesh() = default;
void PreviewBackgroundMesh::Build(unsigned width, unsigned height, const std::vector<std::uint8_t> &colours,
                                  const std::vector<std::uint16_t> &depths, const BackgroundMatrix &cameraToWorld,
                                  const BackgroundMatrix &projection)
{
    native.reset();
    vertices.clear();
    triangles.clear();
    texture.clear();
    textureWidth = textureHeight = 0;
    low = high = {};
    if (width < 2 || height < 2 || width > 1024 || height > 1024 || colours.size() != std::size_t(width) * height ||
        depths.size() != colours.size() || !projection[0] || !projection[5])
        return;
    texture = colours;
    textureWidth = width;
    textureHeight = height;
    std::vector<double> distances(depths.size(), std::numeric_limits<double>::infinity());
    for (std::size_t i = 0; i < depths.size(); ++i)
        if (depths[i] != 0xffff)
        {
            const auto denominator = projection[10] - static_cast<std::int16_t>(depths[i]) / 32768.0;
            const auto distance = projection[14] / denominator;
            if (std::isfinite(distance) && distance > 0)
                distances[i] = distance;
        }
    std::vector<std::int32_t> indices(depths.size(), -1);
    const auto vertex = [&](unsigned x, unsigned y) -> std::uint32_t {
        auto &index = indices[std::size_t(y) * width + x];
        if (index >= 0)
            return static_cast<std::uint32_t>(index);
        const auto distance = distances[std::size_t(y) * width + x];
        // Invert the original pixel-centre viewport and the actual fixed-point
        // projection, not an independently computed FOV approximation.
        const double px = (double(x) - width / 2) / (width / 2) * distance / projection[0];
        const double py = (height / 2 - double(y)) / (height / 2) * distance / projection[5];
        Vertex point = {0, 0, 0, x + .5, y + .5};
        double world[3];
        for (unsigned axis = 0; axis < 3; ++axis)
            world[axis] = px * cameraToWorld[axis] + py * cameraToWorld[4 + axis] - distance * cameraToWorld[8 + axis] +
                          cameraToWorld[12 + axis];
        point.x = world[0];
        point.y = world[1];
        point.z = world[2];
        index = static_cast<std::int32_t>(vertices.size());
        vertices.push_back(point);
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            low[axis] = index == 0 ? float(world[axis]) : std::min(low[axis], float(world[axis]));
            high[axis] = index == 0 ? float(world[axis]) : std::max(high[axis], float(world[axis]));
        }
        return static_cast<std::uint32_t>(index);
    };
    const auto triangle = [&](unsigned ax, unsigned ay, unsigned bx, unsigned by, unsigned cx, unsigned cy) {
        const auto a = distances[std::size_t(ay) * width + ax];
        const auto b = distances[std::size_t(by) * width + bx];
        const auto c = distances[std::size_t(cy) * width + cx];
        if (Continuous(std::min(a, std::min(b, c)), std::max(a, std::max(b, c))))
            triangles.push_back({{vertex(ax, ay), vertex(bx, by), vertex(cx, cy)}});
    };
    std::function<void(unsigned, unsigned, unsigned, unsigned)> cell;
    cell = [&](unsigned left, unsigned top, unsigned right, unsigned bottom) {
        double nearest = std::numeric_limits<double>::infinity(), farthest = 0;
        for (unsigned y = top; y <= bottom; ++y)
            for (unsigned x = left; x <= right; ++x)
            {
                const auto distance = distances[std::size_t(y) * width + x];
                nearest = std::min(nearest, distance);
                farthest = std::max(farthest, distance);
            }
        if (!std::isfinite(nearest))
            return;
        if (!Continuous(nearest, farthest) && (right - left > 1 || bottom - top > 1))
        {
            if (right - left >= bottom - top && right - left > 1)
            {
                const auto middle = (left + right) / 2;
                cell(left, top, middle, bottom);
                cell(middle, top, right, bottom);
            }
            else
            {
                const auto middle = (top + bottom) / 2;
                cell(left, top, right, middle);
                cell(left, middle, right, bottom);
            }
            return;
        }
        triangle(left, top, right, top, right, bottom);
        triangle(left, top, right, bottom, left, bottom);
    };
    for (unsigned y = 0; y + 1 < height; y += 4)
        for (unsigned x = 0; x + 1 < width; x += 4)
            cell(x, y, std::min(width - 1, x + 4), std::min(height - 1, y + 4));
}

bool PreviewBackgroundMesh::AddToScene(br_actor *world)
{
    if (Empty())
        return true;
    if (!native)
    {
        std::unique_ptr<Native> prepared(new Native);
        prepared->map = BrPixelmapAllocate(BR_PMT_INDEX_8, textureWidth, textureHeight, nullptr, BR_PMAF_NORMAL);
        if (!prepared->map)
            return false;
        for (unsigned y = 0; y < textureHeight; ++y)
            std::memcpy(static_cast<byte *>(prepared->map->pixels) + y * prepared->map->row_bytes,
                        texture.data() + std::size_t(y) * textureWidth, textureWidth);
        BrMapAdd(prepared->map);
        prepared->material = BrMaterialAllocate(nullptr);
        if (!prepared->material)
            return false;
        prepared->material->flags = BR_MATF_TWO_SIDED | BR_MATF_PERSPECTIVE | BR_MATF_MAP_OPAQUE;
        prepared->material->colour_map = prepared->map;
        BrMaterialAdd(prepared->material);
        // BRender's prepared vertex and edge indices include signed 16-bit
        // intermediates. Bound even a completely disconnected chunk safely.
        const std::size_t facesPerModel = 4096;
        prepared->models.reserve((triangles.size() + facesPerModel - 1) / facesPerModel);
        std::vector<std::int32_t> indices(vertices.size());
        std::vector<std::uint32_t> selected;
        selected.reserve(facesPerModel * 3);
        for (std::size_t first = 0; first < triangles.size(); first += facesPerModel)
        {
            std::fill(indices.begin(), indices.end(), -1);
            selected.clear();
            const auto count = std::min(facesPerModel, triangles.size() - first);
            for (std::size_t face = first; face < first + count; ++face)
                for (const auto vertex : triangles[face])
                    if (indices[vertex] < 0)
                    {
                        indices[vertex] = static_cast<std::int32_t>(selected.size());
                        selected.push_back(vertex);
                    }
            auto model =
                BrModelAllocate(nullptr, static_cast<br_uint_16>(selected.size()), static_cast<br_uint_16>(count));
            if (!model)
                return false;
            model->flags = BR_MODF_QUICK_UPDATE;
            for (std::size_t i = 0; i < selected.size(); ++i)
            {
                const auto &source = vertices[selected[i]];
                auto &target = model->vertices[i];
                target.p.v[0] = BrFloatToScalar(source.x);
                target.p.v[1] = BrFloatToScalar(source.y);
                target.p.v[2] = BrFloatToScalar(source.z);
                target.map.v[0] = BrFloatToScalar(source.u / textureWidth);
                target.map.v[1] = BrFloatToScalar(source.v / textureHeight);
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                auto &target = model->faces[i];
                target.material = prepared->material;
                for (unsigned corner = 0; corner < 3; ++corner)
                    target.vertices[corner] = static_cast<br_uint_16>(indices[triangles[first + i][corner]]);
            }
            BrModelAdd(model);
            prepared->models.push_back(model);
        }
        native.swap(prepared);
    }
    for (auto model : native->models)
    {
        auto actor = BrActorAllocate(BR_ACTOR_MODEL, nullptr);
        if (!actor)
            return false;
        actor->model = model;
        actor->material = native->material;
        actor->t.type = BR_TRANSFORM_IDENTITY;
        BrActorAdd(world, actor);
    }
    return true;
}
} // namespace Internal
} // namespace ThreeMM
