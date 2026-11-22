#include "caption_quad.h"
#include "mm/caption_render.h"
#include <algorithm>

MMPreviewProjection marcus_caption_projection(const MM::PreviewProjection &source)
{
    MMPreviewProjection result{};
    result.valid = source.valid;
    result.orthographic = source.orthographic;
    std::copy_n(source.transform, 12, result.transform);
    result.vertical_fov = source.verticalFov;
    result.aspect = source.aspect;
    result.near_clip = source.nearClip;
    result.far_clip = source.farClip;
    result.display_depth = source.displayDepth;
    result.ortho_height = source.orthoHeight;
    return result;
}

void marcus_caption_quad(const MM::PreviewImage &texture, const MM::PreviewProjection &camera,
                         MM::PreviewImage &scene)
{
    const auto authored = marcus_caption_projection(camera);
    const auto view = marcus_caption_projection(scene.projection);
    auto covered = static_cast<MMU32>(scene.coveredPixels);
    mm_caption_quad(texture.rgba.data(), texture.rgba.size(), texture.width, texture.height, &authored, &view,
                    scene.rgba.data(), scene.rgba.size(), scene.width, scene.height,
                    scene.depth.data(), scene.depth.size(), &covered);
    scene.coveredPixels = covered;
}
