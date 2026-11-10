#include "caption_quad.h"
#include <algorithm>
#include <cmath>

namespace
{
bool Valid(const ThreeMM::PreviewProjection &camera)
{
    if (!camera.valid || !std::isfinite(camera.verticalFov) || camera.verticalFov <= 0 || camera.verticalFov >= 179 ||
        !std::isfinite(camera.aspect) || camera.aspect <= 0 || !std::isfinite(camera.nearClip) ||
        camera.nearClip <= 0 || !std::isfinite(camera.farClip) || camera.farClip <= camera.nearClip)
        return false;
    if (camera.orthographic && (!std::isfinite(camera.orthoHeight) || camera.orthoHeight <= 0))
        return false;
    for (auto value : camera.transform)
        if (!std::isfinite(value))
            return false;
    return true;
}
} // namespace
void marcus_caption_quad(const ThreeMM::PreviewImage &texture, const ThreeMM::PreviewProjection &camera,
                         ThreeMM::PreviewImage &scene)
{
    const auto &view = scene.projection;
    if (!Valid(camera) || !Valid(view) || !std::isfinite(camera.displayDepth) || camera.displayDepth <= 0 ||
        !texture.width || !texture.height || !scene.width || !scene.height ||
        texture.rgba.size() != std::size_t(texture.width) * texture.height * 4 ||
        scene.rgba.size() != std::size_t(scene.width) * scene.height * 4)
        return;
    double inverse[3][3];
    const auto &matrix = camera.transform;
    for (int row = 0; row < 3; ++row)
        for (int axis = 0; axis < 3; ++axis)
            inverse[row][axis] =
                matrix[((row + 1) % 3) * 3 + (axis + 1) % 3] * double(matrix[((row + 2) % 3) * 3 + (axis + 2) % 3]) -
                matrix[((row + 1) % 3) * 3 + (axis + 2) % 3] * double(matrix[((row + 2) % 3) * 3 + (axis + 1) % 3]);
    const auto determinant = matrix[0] * inverse[0][0] + matrix[1] * inverse[0][1] + matrix[2] * inverse[0][2];
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
        return;
    double origin[3] = {}, basis[3][3] = {};
    for (int row = 0; row < 3; ++row)
        for (int axis = 0; axis < 3; ++axis)
        {
            inverse[row][axis] /= determinant;
            origin[row] += inverse[row][axis] * (view.transform[9 + axis] - matrix[9 + axis]);
            for (int direction = 0; direction < 3; ++direction)
                basis[row][direction] += inverse[row][axis] * view.transform[direction * 3 + axis];
        }
    const auto halfHeight = std::tan(camera.verticalFov * .008726646259971648) * camera.displayDepth;
    const auto halfWidth = halfHeight * camera.aspect;
    const auto tangentY = view.orthographic ? view.orthoHeight * .5 : std::tan(view.verticalFov * .008726646259971648);
    const auto tangentX = tangentY * view.aspect;
    const auto hasDepth = scene.depth.size() == std::size_t(scene.width) * scene.height;
    // Intersect inspection-camera rays with the authored image plane. This
    // inverse projection is perspective-correct and naturally clips a quad
    // crossing the near plane, including a viewer flying through the plane.
    for (std::uint32_t y = 0; y < scene.height; ++y)
    {
        const auto rayY = (1 - 2 * (y + .5) / scene.height) * tangentY;
        for (std::uint32_t x = 0; x < scene.width; ++x)
        {
            const auto rayX = (2 * (x + .5) / scene.width - 1) * tangentX;
            double rayOrigin[3], direction[3];
            for (int axis = 0; axis < 3; ++axis)
            {
                const auto offset = basis[axis][0] * rayX + basis[axis][1] * rayY;
                rayOrigin[axis] = origin[axis] + (view.orthographic ? offset : 0);
                direction[axis] = -basis[axis][2] + (view.orthographic ? 0 : offset);
            }
            if (std::abs(direction[2]) < 1e-12)
                continue;
            const auto distance = (-camera.displayDepth - rayOrigin[2]) / direction[2];
            if (!std::isfinite(distance) || distance < view.nearClip || distance > view.farClip)
                continue;
            const auto pixel = std::size_t(y) * scene.width + x;
            if (hasDepth && distance > scene.depth[pixel] + .001)
                continue;
            const auto u = .5 + (rayOrigin[0] + direction[0] * distance) / (2 * halfWidth);
            const auto v = .5 - (rayOrigin[1] + direction[1] * distance) / (2 * halfHeight);
            if (u < 0 || u >= 1 || v < 0 || v >= 1)
                continue;
            const auto tx = static_cast<std::uint32_t>(u * texture.width);
            const auto ty = static_cast<std::uint32_t>(v * texture.height);
            const auto source = &texture.rgba[(std::size_t(ty) * texture.width + tx) * 4];
            const unsigned alpha = source[3];
            if (!alpha)
                continue;
            auto target = &scene.rgba[pixel * 4];
            const unsigned coverage = alpha * 255 + target[3] * (255 - alpha);
            if (!target[3])
                ++scene.coveredPixels;
            for (int channel = 0; channel < 3; ++channel)
                target[channel] = static_cast<std::uint8_t>(
                    (source[channel] * alpha * 255 + target[channel] * target[3] * (255 - alpha)) / coverage);
            target[3] = static_cast<std::uint8_t>(coverage / 255);
        }
    }
}
