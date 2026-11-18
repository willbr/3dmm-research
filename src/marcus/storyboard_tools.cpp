#include "storyboard_tools.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr float radians = .01745329252f;
using Point = std::array<float, 3>;
float Dot(const Point &a, const Point &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Point Add(const Point &a, const Point &b, float factor = 1)
{ return {a[0] + b[0] * factor, a[1] + b[1] * factor, a[2] + b[2] * factor}; }
Point Forward(const MarcusBoardLight &light)
{
    const float cp = std::cos(light.pitch * radians);
    return {-std::sin(light.yaw * radians) * cp, -std::sin(light.pitch * radians), -std::cos(light.yaw * radians) * cp};
}
Point Ray(const MarcusProjection &view, float x, float y)
{
    const float tangent = std::tan(view.vertical_fov * radians / 2);
    Point ray;
    for (int i = 0; i < 3; ++i)
        ray[i] = view.transform[i] * (2 * x - 1) * tangent * view.aspect +
            view.transform[3 + i] * (1 - 2 * y) * tangent - view.transform[6 + i];
    return ray;
}
}

MarcusProjection MarcusBoardProjection(const MarcusBoardAngle &a)
{
    MarcusProjection projection;
    const auto pose = MarcusBoardPoseOf(a);
    marcus_board_projection(&pose, &projection);
    return projection;
}

MarcusBoardLightWidget MarcusBoardBuildLightWidget(const MarcusProjection &view, const MarcusBoardLight &light, float viewportHeight)
{
    MarcusBoardLightWidget widget;
    const Point centre = {light.x, light.y, light.z}, forward = Forward(light);
    const Point back = {view.transform[6], view.transform[7], view.transform[8]};
    const Point eye = {view.transform[9], view.transform[10], view.transform[11]};
    const float distance = Dot(Add(eye, centre, -1), back);
    if (!view.valid || viewportHeight <= 0 || distance <= view.near_clip) return widget;
    widget.scale = 70 * 2 * distance * std::tan(view.vertical_fov * radians / 2) / viewportHeight;
    if (!std::isfinite(widget.scale) || widget.scale <= 0) return widget;
    auto segment = [&](MarcusCameraGuide &guide, const Point &a, const Point &b) {
        camera_guide_add_line(&view, a.data(), b.data(), &guide);
    };
    widget.visible = camera_project_point(&view, centre.data(), widget.centre.data()) != 0;
    const float radius = widget.scale * .18f;
    if (light.point)
    {
        for (int plane = 0; plane < 3; ++plane) for (int edge = 0; edge < 8; ++edge)
        {
            Point a = centre, b = centre;
            a[plane] += radius * std::cos(edge * 45 * radians); a[(plane + 1) % 3] += radius * std::sin(edge * 45 * radians);
            b[plane] += radius * std::cos((edge + 1) * 45 * radians); b[(plane + 1) % 3] += radius * std::sin((edge + 1) * 45 * radians);
            segment(widget.shape, a, b);
        }
    }
    else
    {
        const Point right = {std::cos(light.yaw * radians), 0, -std::sin(light.yaw * radians)};
        const Point up = {-std::sin(light.yaw * radians) * std::sin(light.pitch * radians), std::cos(light.pitch * radians),
            -std::cos(light.yaw * radians) * std::sin(light.pitch * radians)};
        for (int edge = 0; edge < 8; ++edge)
        {
            const auto a = Add(Add(centre, right, radius * std::cos(edge * 45 * radians)), up, radius * std::sin(edge * 45 * radians));
            const auto b = Add(Add(centre, right, radius * std::cos((edge + 1) * 45 * radians)), up, radius * std::sin((edge + 1) * 45 * radians));
            segment(widget.shape, a, b);
        }
        const auto tip = Add(centre, forward, widget.scale);
        segment(widget.direction, centre, tip);
        for (const auto &side : {right, up}) for (int sign : {-1, 1})
            segment(widget.direction, tip, Add(Add(tip, forward, -.25f * widget.scale), side, sign * radius));
    }
    return widget;
}

void MarcusBoardLightTransform(const MarcusBoardLight &light, float matrix[16])
{
    const auto values = MarcusBoardLightValuesOf(light);
    marcus_board_light_matrix(&values, matrix);
}
bool MarcusBoardLightFromTransform(const MarcusBoardLight &original, const float matrix[16], bool rotate, MarcusBoardLight &light)
{
    const auto values = MarcusBoardLightValuesOf(original);
    MarcusBoardLightValues next;
    if (!marcus_board_light_from_matrix(&values, matrix, rotate, &next)) return false;
    light = MarcusBoardLightFromValues(next);
    return true;
}

MarcusBoardGridFade::MarcusBoardGridFade(const MarcusBoardAngle &angle, float width, float height)
{
    view = MarcusBoardProjection(angle);
    tangent = std::tan(angle.fov * .00872664626);
    const double step = 5 * std::max(1., std::ceil(std::abs(angle.camera.y) / 10.));
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 || tangent <= 0)
    { view.valid = 0; return; }
    scaleX = width * step / (2 * tangent * view.aspect);
    scaleY = height * step / (2 * tangent);
}
float MarcusBoardGridFade::Opacity(double x, double y) const
{
    if (!view.valid || !std::isfinite(x) || !std::isfinite(y)) return 0;
    const double cx = (2 * x - 1) * tangent * view.aspect, cy = (1 - 2 * y) * tangent;
    const auto *m = view.transform;
    const double groundRay = m[1] * cx + m[4] * cy - m[7];
    if (std::abs(groundRay) < 1e-10) return 0;
    const double distance = -m[10] / groundRay;
    if (!std::isfinite(distance) || distance < view.near_clip || distance > view.far_clip) return 0;
    // Project the ground plane's X/Z derivatives. Area divided by either edge
    // gives the perpendicular spacing between that family of parallel lines.
    const double xx = scaleX * (m[0] + cx * m[6]) / distance;
    const double xy = -scaleY * (m[3] + cy * m[6]) / distance;
    const double zx = scaleX * (m[2] + cx * m[8]) / distance;
    const double zy = -scaleY * (m[5] + cy * m[8]) / distance;
    const double edge = std::max(std::hypot(xx, xy), std::hypot(zx, zy));
    if (edge < 1e-10) return 0;
    const double spacing = std::abs(xx * zy - xy * zx) / edge;
    const double fade = std::clamp((spacing - 1) / 5, 0., 1.);
    return std::isfinite(fade) ? float(fade * fade * (3 - 2 * fade)) : 0;
}

std::vector<std::array<double, 4>> MarcusBoardVisibleGridLines(const MarcusBoardAngle &angle,
    const std::vector<float> &depth, int width, int height)
{
    const auto lines = MarcusBoardGridLines(angle);
    if (width <= 0 || height <= 0 || depth.size() != size_t(width) * height) return lines;
    std::vector<std::array<double, 4>> visible;
    const auto projection = MarcusBoardProjection(angle);
    for (const auto &line : lines)
    {
        const int steps = std::max(1, int(std::ceil(std::max(std::abs(line[2] - line[0]) * width, std::abs(line[3] - line[1]) * height) * 2)));
        int start = -1;
        auto point = [&](double t) { return std::array<double, 2>{line[0] + (line[2] - line[0]) * t, line[1] + (line[3] - line[1]) * t}; };
        for (int step = 0; step <= steps; ++step)
        {
            const auto p = point((step + .5) / (steps + 1));
            const auto ray = Ray(projection, float(p[0]), float(p[1]));
            const float distance = std::abs(ray[1]) > 1e-8f ? -angle.camera.y / ray[1] : 0;
            const int px = std::clamp(int(p[0] * width), 0, width - 1), py = std::clamp(int(p[1] * height), 0, height - 1);
            const float scene = depth[size_t(py) * width + px];
            const bool show = distance >= angle.nearClip && distance <= angle.farClip && (!std::isfinite(scene) || distance <= scene + .01f);
            if (show && start < 0) start = step;
            if (start >= 0 && (!show || step == steps))
            {
                const auto a = point(double(start) / (steps + 1)), b = point(double(show ? step + 1 : step) / (steps + 1));
                visible.push_back({a[0], a[1], b[0], b[1]}); start = -1;
            }
        }
    }
    return visible;
}
