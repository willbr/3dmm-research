#include "preview_background_mesh.h"
#include "preview_depth_range.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "soc_core.h"
#pragma pack(push, 4)
#include "zb.h"
#pragma pack(pop)
#undef size
ASSERTNAME

using namespace ThreeMM::Internal;
void Check(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "Background mesh check failed at line %d\n", line);
        std::exit(1);
    }
}
#define CHECK(value) Check((value), __LINE__)
BackgroundMatrix Identity()
{
    BackgroundMatrix result = {};
    result[0] = result[5] = result[10] = result[15] = 1;
    return result;
}
BackgroundMatrix Projection()
{
    BackgroundMatrix result = {};
    result[0] = result[5] = result[10] = 1;
    result[11] = -1;
    result[14] = 2;
    return result;
}
struct Target
{
    static constexpr unsigned width = 17, height = 13, stride = 20;
    std::vector<std::uint8_t> colour;
    std::vector<std::uint16_t> depth;
    Target() : colour(stride * height, 199), depth(stride * height, 0xffff)
    {
    }
    void Render(PreviewBackgroundMesh &mesh, const BackgroundMatrix &projection)
    {
        auto colours = BrPixelmapAllocate(BR_PMT_INDEX_8, width, height, colour.data(), BR_PMAF_NORMAL);
        auto depths = BrPixelmapAllocate(BR_PMT_DEPTH_16, stride, height, depth.data(), BR_PMAF_NORMAL);
        depths->width = width;
        auto world = BrActorAllocate(BR_ACTOR_NONE, nullptr);
        br_camera camera = {};
        camera.type = BR_CAMERA_PERSPECTIVE_FOV;
        camera.field_of_view = BR_ANGLE_DEG(90);
        camera.aspect = BR_SCALAR(1);
        camera.hither_z = BR_SCALAR(1);
        camera.yon_z = BR_SCALAR(1000);
        auto eye = BrActorAllocate(BR_ACTOR_CAMERA, &camera);
        BrActorAdd(world, eye);
        CHECK(mesh.AddToScene(world));
        BrZbSceneRenderBegin(world, eye, colours, depths);
        // Supply an exact synthetic clip transform to BRender. Its normal
        // model/face culling, clipping, texturing and depth testing still run.
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column)
                fw.view_to_screen.m[row][column] = fw.model_to_screen.m[row][column] =
                    BrFloatToScalar(projection[row * 4 + column]);
        BrZbSceneRenderAdd(world);
        BrZbSceneRenderEnd();
        BrActorFree(world);
        BrPixelmapFree(depths);
        BrPixelmapFree(colours);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = width; x < stride; ++x)
            {
                CHECK(colour[y * stride + x] == 199);
                CHECK(depth[y * stride + x] == 0xffff);
            }
    }
    unsigned Covered() const
    {
        return static_cast<unsigned>(
            std::count_if(depth.begin(), depth.end(), [](std::uint16_t z) { return z != 0xffff; }));
    }
};
int main()
{
    // A tight interval separates nearby surfaces that quantize to the same
    // 16-bit value in the old 0.1..30000 inspection range.
    PreviewDepthRange fitted(1, 1, .1, 30000);
    fitted.Include({{{-1, -1, 100}, {1, -1, 100}, {0, 1, 100}}});
    fitted.Include({{{-1, -1, 100.05}, {1, -1, 100.05}, {0, 1, 100.05}}});
    const auto interval = fitted.Fit();
    CHECK(interval.first == 50 && interval.second > 100.05 && interval.second < 106);
    const auto depth = [](double z, double near, double far) {
        return std::floor(65535 * far / (far - near) * (1 - near / z));
    };
    CHECK(depth(100, .1, 30000) == depth(100.05, .1, 30000));
    CHECK(depth(100, interval.first, interval.second) < depth(100.05, interval.first, interval.second));
    PreviewDepthRange close(1, 1, .1, 30000);
    close.Include({{{-100, -100, 1}, {100, -100, 1}, {0, 100, 1}}});
    CHECK(close.Fit().first == .5); // A visible face with all vertices outside the viewport.
    PreviewDepthRange crossing(1, 1, .1, 30000);
    crossing.Include({{{.01, -1, -1}, {.01, 1, -1}, {.01, 0, 2}}});
    CHECK(crossing.Fit().first == .1); // Keep surfaces crossing the original near plane.
    PreviewDepthRange empty(1, 1, .1, 30000);
    empty.Include({{{100, 0, 1}, {101, 0, 1}, {100, 1, 1}}});
    CHECK(empty.Fit() == std::make_pair(.1, 30000.0));
    PreviewDepthRange boundary(1, 1, .1, 30000);
    boundary.Include({{{-1, -1, 1}, {2, -1, 1}, {-1, 2, 1}}});
    CHECK(boundary.Fit() == std::make_pair(.5, 1.05)); // Vertices exactly on clipping planes.
    BrBegin();
    BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
    const auto initialModels = BrModelCount(nullptr);
    const auto initialMaps = BrMapCount(nullptr);
    const auto initialMaterials = BrMaterialCount(nullptr);
    std::vector<std::uint8_t> colours(Target::width * Target::height);
    std::vector<std::uint16_t> depths(colours.size(), 26214); // About ten units forward.
    for (unsigned i = 0; i < colours.size(); ++i)
        colours[i] = static_cast<std::uint8_t>(i);
    PreviewBackgroundMesh mesh;
    mesh.Build(Target::width, Target::height, colours, depths, Identity(), Projection());
    CHECK(!mesh.Empty() && mesh.TriangleCount() < 100);
    CHECK(mesh.InfiniteColours().empty());
    CHECK(std::abs(mesh.low[2] + 10) < .01 && std::abs(mesh.high[2] + 10) < .01);
    Target same;
    same.Render(mesh, Projection());
    CHECK(same.Covered() == colours.size());
    for (unsigned y = 0; y < Target::height; ++y)
        for (unsigned x = 0; x < Target::width; ++x)
        {
            CHECK(same.colour[y * Target::stride + x] == colours[y * Target::width + x]);
            CHECK(std::abs(int(same.depth[y * Target::stride + x]) - 26214) <= 1);
        }
    CHECK(same.colour[0] == 0 && same.depth[0] != 0xffff); // Index zero is opaque.

    // Existing foreground depth must survive; existing farther pixels are replaced.
    Target occlusion;
    occlusion.depth[6 * Target::stride + 8] = 100;
    occlusion.Render(mesh, Projection());
    CHECK(occlusion.colour[6 * Target::stride + 8] == 199);
    CHECK(occlusion.depth[6 * Target::stride + 8] == 100);
    CHECK(occlusion.depth[6 * Target::stride + 7] < 0xffff);

    // Parallax: shift a free camera right. Colour and depth both move.
    auto translated = Projection();
    translated[12] = -3;
    Target moved;
    moved.Render(mesh, translated);
    CHECK(moved.Covered() && moved.Covered() < same.Covered());
    CHECK(moved.colour != same.colour);

    // Camera-to-world translation must cancel its inverse at render time.
    auto placement = Identity();
    placement[12] = 3;
    mesh.Build(Target::width, Target::height, colours, depths, placement, Projection());
    Target restored;
    restored.Render(mesh, translated);
    CHECK(restored.colour == same.colour && restored.depth == same.depth);

    // Observe the rear of the reconstructed sheet, using a reversed camera.
    auto rear = Projection();
    rear[0] = -1;
    rear[10] = -1;
    rear[11] = 1;
    rear[14] = -18;
    rear[15] = 20;
    mesh.Build(Target::width, Target::height, colours, depths, Identity(), Projection());
    Target backside;
    backside.Render(mesh, rear);
    CHECK(backside.Covered());

    // An oblique free camera looking at a flat textured plane has an analytic
    // ray/plane intersection. Check UVs against that, independently of the
    // triangle interpolation (affine UV interpolation fails this comparison).
    const auto s = std::sqrt(.5);
    const auto planeDepth = 2 / (1 - 26214 / 32768.0);
    auto perspective = Projection();
    perspective[0] = s;
    perspective[2] = s;
    perspective[3] = -s;
    perspective[8] = -s;
    perspective[10] = s;
    perspective[11] = -s;
    perspective[12] = -10 * s;
    perspective[14] = 2 - 4 * s;
    perspective[15] = 4 * s;
    Target angled;
    angled.Render(mesh, perspective);
    unsigned verified = 0;
    for (unsigned y = 0; y < Target::height; ++y)
        for (unsigned x = 0; x < Target::width; ++x)
        {
            const auto nx = (double(x) - Target::width / 2) / (Target::width / 2);
            const auto ny = (Target::height / 2 - double(y)) / (Target::height / 2);
            if (nx <= -1)
                continue;
            const auto distance = (planeDepth - 3) / (s * (nx + 1));
            const auto worldX = 7 + distance * s * (nx - 1);
            const auto u = Target::width / 2 + .5 + Target::width / 2 * worldX / planeDepth;
            const auto v = Target::height / 2 + .5 - Target::height / 2 * distance * ny / planeDepth;
            if (u < .51 || u > Target::width - .51 || v < .51 || v > Target::height - .51 ||
                std::abs(u - std::round(u)) < .001 || std::abs(v - std::round(v)) < .001)
                continue;
            CHECK(angled.depth[y * Target::stride + x] != 0xffff);
            CHECK(angled.colour[y * Target::stride + x] ==
                  colours[unsigned(std::floor(v)) * Target::width + unsigned(std::floor(u))]);
            ++verified;
        }
    CHECK(verified > 50);

    // Entirely behind the eye and outside the far/near clip planes.
    auto hidden = Projection();
    hidden[15] = -20;
    Target behind;
    behind.Render(mesh, hidden);
    CHECK(!behind.Covered());
    auto near = Projection();
    near[14] = 20;
    Target tooNear;
    tooNear.Render(mesh, near);
    CHECK(!tooNear.Covered());
    auto far = Projection();
    far[14] = -20;
    Target tooFar;
    tooFar.Render(mesh, far);
    CHECK(!tooFar.Covered());
    auto nearCrossing = Projection();
    nearCrossing[2] = .5;
    nearCrossing[14] = 10;
    Target partialNear;
    partialNear.Render(mesh, nearCrossing);
    CHECK(partialNear.Covered() && partialNear.Covered() < same.Covered());
    auto farCrossing = Projection();
    farCrossing[2] = .5;
    farCrossing[14] = 0;
    Target partialFar;
    partialFar.Render(mesh, farCrossing);
    CHECK(partialFar.Covered() && partialFar.Covered() < same.Covered());

    // A missing stripe must stay a hole, including when coarse cells straddle it.
    for (unsigned y = 0; y < Target::height; ++y)
        depths[y * Target::width + 7] = 0xffff;
    mesh.Build(Target::width, Target::height, colours, depths, Identity(), Projection());
    Target holes;
    holes.Render(mesh, Projection());
    CHECK(holes.Covered() && holes.Covered() < same.Covered());
    CHECK(mesh.InfiniteColours().size() == colours.size());
    for (unsigned y = 0; y < Target::height; ++y)
    {
        CHECK(holes.depth[y * Target::stride + 7] == 0xffff);
        for (unsigned x = 0; x < Target::width; ++x)
            CHECK(mesh.InfiniteColours()[y * Target::width + x] == colours[y * Target::width + 7]);
    }

    // Two planes meet at a hard depth edge. A rotated inspection camera must
    // see an opening there rather than triangles stretched between the planes.
    for (unsigned y = 0; y < Target::height; ++y)
        for (unsigned x = 0; x < Target::width; ++x)
            depths[y * Target::width + x] = x < 8 ? 19660 : 29491; // 5 / 20 units.
    mesh.Build(Target::width, Target::height, colours, depths, Identity(), Projection());
    auto oblique = Projection();
    oblique[0] = .8;
    oblique[2] = .6;
    oblique[3] = -.6;
    oblique[8] = -.6;
    oblique[10] = .8;
    oblique[11] = -.8;
    Target edge;
    edge.Render(mesh, oblique);
    CHECK(edge.Covered());
    CHECK(edge.depth[6 * Target::stride + 13] == 0xffff);

    // Every vertex is outside a side plane but the triangle crosses the viewport.
    auto enlarged = Projection();
    enlarged[0] = enlarged[5] = 4;
    Target clipped;
    clipped.Render(mesh, enlarged);
    CHECK(clipped.Covered());

    // A maximum-size depth image exceeds BRender's 16-bit model indices.
    // Its cached chunks must all render safely, then leave no registrations
    // behind when a different camera replaces the mesh.
    std::vector<std::uint8_t> largeColours(1024 * 1024, 42);
    std::vector<std::uint16_t> largeDepths(largeColours.size(), 26214);
    mesh.Build(1024, 1024, largeColours, largeDepths, Identity(), Projection());
    CHECK(mesh.InfiniteColours().empty());
    CHECK(mesh.TriangleCount() > 65535);
    Target large;
    large.Render(mesh, Projection());
    CHECK(large.Covered() > 100 && large.colour[6 * Target::stride + 8] == 42);
    CHECK(BrModelCount(nullptr) > initialModels + 1);
    Target cached;
    cached.Render(mesh, Projection());
    CHECK(cached.colour == large.colour && cached.depth == large.depth);

    // Empty / malformed inputs clear an earlier cached result safely.
    std::fill(depths.begin(), depths.end(), 0xffff);
    mesh.Build(Target::width, Target::height, colours, depths, Identity(), Projection());
    CHECK(mesh.Empty());
    CHECK(mesh.InfiniteColours() == colours); // Includes opaque palette index zero.
    CHECK(BrModelCount(nullptr) == initialModels && BrMapCount(nullptr) == initialMaps &&
          BrMaterialCount(nullptr) == initialMaterials);
    mesh.Build(1025, 2, colours, depths, Identity(), Projection());
    CHECK(mesh.Empty());
    CHECK(mesh.InfiniteColours().empty());
    mesh.Build(1, 1, {0}, {0xffff}, Identity(), Projection());
    CHECK(mesh.Empty() && mesh.InfiniteColours() == std::vector<std::uint8_t>{0});
    BrZbEnd();
    BrEnd();
    std::puts("Background mesh: reconstruction, parallax, transforms, depth, texture, gaps and clipping passed.");
    return 0;
}
