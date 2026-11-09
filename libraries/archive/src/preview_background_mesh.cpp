#include "preview_background_mesh.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

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
struct ClipVertex
{
    double x, y, z, w, u, v;
};
double Plane(const ClipVertex &v, unsigned plane)
{
    switch (plane)
    {
    case 0:
        return v.w + v.x;
    case 1:
        return v.w - v.x;
    case 2:
        return v.w + v.y;
    case 3:
        return v.w - v.y;
    // BRender clips to -W <= Z <= 0, rather than a symmetric Z range.
    case 4:
        return -v.z;
    default:
        return v.w + v.z;
    }
}
ClipVertex Mix(const ClipVertex &a, const ClipVertex &b, double t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t};
}
struct ScreenVertex
{
    double x, y, z, reciprocalW, u, v;
};
double Edge(const ScreenVertex &a, const ScreenVertex &b, double x, double y)
{
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}
} // namespace

void PreviewBackgroundMesh::Build(unsigned width, unsigned height, const std::vector<std::uint8_t> &colours,
                                  const std::vector<std::uint16_t> &depths, const BackgroundMatrix &cameraToWorld,
                                  const BackgroundMatrix &projection)
{
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

void PreviewBackgroundMesh::Render(const BackgroundMatrix &matrix, unsigned width, unsigned height,
                                   std::uint8_t *colour, unsigned colourStride, std::uint8_t *depth,
                                   unsigned depthStride) const
{
    if (Empty() || !width || !height || !colour || !depth)
        return;
    std::vector<ClipVertex> transformed;
    transformed.reserve(vertices.size());
    for (const auto &v : vertices)
        transformed.push_back({v.x * matrix[0] + v.y * matrix[4] + v.z * matrix[8] + matrix[12],
                               v.x * matrix[1] + v.y * matrix[5] + v.z * matrix[9] + matrix[13],
                               v.x * matrix[2] + v.y * matrix[6] + v.z * matrix[10] + matrix[14],
                               v.x * matrix[3] + v.y * matrix[7] + v.z * matrix[11] + matrix[15], v.u, v.v});
    const auto rasterize = [&](const ClipVertex &a, const ClipVertex &b, const ClipVertex &c) {
        ScreenVertex screen[3];
        const ClipVertex input[] = {a, b, c};
        for (unsigned i = 0; i < 3; ++i)
        {
            const auto &v = input[i];
            if (v.w <= 0 || !std::isfinite(v.w))
                return;
            const auto reciprocal = 1 / v.w;
            screen[i] = {width / 2 + .5 + width / 2 * v.x * reciprocal,
                         height / 2 + .5 - height / 2 * v.y * reciprocal,
                         -v.z * reciprocal * 32768,
                         reciprocal,
                         v.u * reciprocal,
                         v.v * reciprocal};
        }
        auto &p = screen[0];
        auto &q = screen[1];
        auto &r = screen[2];
        auto area = Edge(p, q, r.x, r.y);
        if (!std::isfinite(area) || std::abs(area) < 1e-9)
            return;
        if (area < 0)
        {
            std::swap(q, r);
            area = -area;
        }
        const auto left = std::max(0, int(std::ceil(std::min(p.x, std::min(q.x, r.x)) - .5)));
        const auto right = std::min(int(width) - 1, int(std::floor(std::max(p.x, std::max(q.x, r.x)) - .5)));
        const auto top = std::max(0, int(std::ceil(std::min(p.y, std::min(q.y, r.y)) - .5)));
        const auto bottom = std::min(int(height) - 1, int(std::floor(std::max(p.y, std::max(q.y, r.y)) - .5)));
        const auto inverseArea = 1 / area;
        for (int y = top; y <= bottom; ++y)
        {
            auto pixels = colour + std::size_t(y) * colourStride;
            auto depths = reinterpret_cast<std::uint16_t *>(depth + std::size_t(y) * depthStride);
            for (int x = left; x <= right; ++x)
            {
                const auto wa = Edge(q, r, x + .5, y + .5) * inverseArea;
                const auto wb = Edge(r, p, x + .5, y + .5) * inverseArea;
                const auto wc = 1 - wa - wb;
                if (wa < -1e-9 || wb < -1e-9 || wc < -1e-9)
                    continue;
                const auto z = static_cast<std::uint16_t>(
                    std::max(0., std::min(32767., std::floor(wa * p.z + wb * q.z + wc * r.z))));
                if (z >= depths[x])
                    continue;
                const auto reciprocal = wa * p.reciprocalW + wb * q.reciprocalW + wc * r.reciprocalW;
                const auto u = (wa * p.u + wb * q.u + wc * r.u) / reciprocal;
                const auto v = (wa * p.v + wb * q.v + wc * r.v) / reciprocal;
                const auto tx = std::max(0, std::min(int(textureWidth) - 1, int(std::floor(u))));
                const auto ty = std::max(0, std::min(int(textureHeight) - 1, int(std::floor(v))));
                pixels[x] = texture[std::size_t(ty) * textureWidth + tx];
                depths[x] = z;
            }
        }
    };
    for (const auto &triangle : triangles)
    {
        // Six convex clip planes can grow a triangle to at most nine vertices.
        std::array<ClipVertex, 12> polygon, clipped;
        unsigned count = 3;
        for (unsigned i = 0; i < 3; ++i)
            polygon[i] = transformed[triangle[i]];
        for (unsigned plane = 0; plane < 6 && count; ++plane)
        {
            unsigned next = 0;
            auto previous = polygon[count - 1];
            auto previousDistance = Plane(previous, plane);
            for (unsigned i = 0; i < count; ++i)
            {
                const auto current = polygon[i];
                const auto distance = Plane(current, plane);
                if ((distance >= 0) != (previousDistance >= 0))
                    clipped[next++] = Mix(previous, current, previousDistance / (previousDistance - distance));
                if (distance >= 0)
                    clipped[next++] = current;
                previous = current;
                previousDistance = distance;
            }
            polygon.swap(clipped);
            count = next;
        }
        for (unsigned i = 1; i + 1 < count; ++i)
            rasterize(polygon[0], polygon[i], polygon[i + 1]);
    }
}
} // namespace Internal
} // namespace ThreeMM
