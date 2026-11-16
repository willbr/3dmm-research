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
bool Intersect(const Point &eye, const Point &ray, const Point &point, const Point &normal, Point &out)
{
    const float denominator = Dot(ray, normal);
    if (std::abs(denominator) < .00001f) return false;
    const float distance = Dot(Add(point, eye, -1), normal) / denominator;
    if (!std::isfinite(distance) || distance <= 0) return false;
    out = Add(eye, ray, distance);
    return true;
}
}

MarcusProjection MarcusBoardProjection(const MarcusBoardAngle &a)
{
    MarcusProjection p = {};
    const float cy = std::cos(a.camera.yaw * radians), sy = std::sin(a.camera.yaw * radians);
    const float cp = std::cos(a.camera.pitch * radians), sp = std::sin(a.camera.pitch * radians);
    const float cr = std::cos(a.roll * radians), sr = std::sin(a.roll * radians);
    const float matrix[] = {cy * cr - sy * sp * sr, cp * sr, -sy * cr - cy * sp * sr,
        -cy * sr - sy * sp * cr, cp * cr, sy * sr - cy * sp * cr,
        sy * cp, sp, cy * cp, a.camera.x, a.camera.y, a.camera.z};
    std::copy(matrix, matrix + 12, p.transform);
    p.valid = 1; p.vertical_fov = a.fov; p.aspect = float(MarcusBoardWidth) / MarcusBoardHeight;
    p.near_clip = a.nearClip; p.far_clip = a.farClip; p.display_depth = 10;
    return p;
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
    auto handle = [&](int i, const Point &p) { widget.visible[i] = camera_project_point(&view, p.data(), widget.handles[i].data()) != 0; };
    handle(0, centre);
    for (int axis = 0; axis < 3; ++axis)
    {
        Point tip = centre; tip[axis] += widget.scale;
        handle(axis + 1, tip); segment(widget.axes[axis], centre, tip);
        if (widget.visible[0] && widget.visible[axis + 1] &&
            std::hypot((widget.handles[axis + 1][0] - widget.handles[0][0]) * view.aspect,
                widget.handles[axis + 1][1] - widget.handles[0][1]) * viewportHeight < 16)
        { widget.visible[axis + 1] = false; widget.axes[axis] = {}; }
    }
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
        handle(4, tip);
    }
    return widget;
}

bool MarcusBoardDragLight(const MarcusProjection &view, const MarcusBoardLight &original, int handle, float scale,
    float startX, float startY, float x, float y, MarcusBoardLight &light)
{
    if (!view.valid || view.orthographic || handle < 0 || handle > 4 || (handle == 4 && original.point) ||
        !std::isfinite(scale) || scale <= 0 || !std::isfinite(x) || !std::isfinite(y)) return false;
    const Point eye = {view.transform[9], view.transform[10], view.transform[11]};
    const Point centre = {original.x, original.y, original.z};
    const auto firstRay = Ray(view, startX, startY), ray = Ray(view, x, y);
    Point normal = {view.transform[6], view.transform[7], view.transform[8]};
    Point plane = centre, first, last;
    if (handle > 0 && handle < 4)
    {
        normal = firstRay; normal[handle - 1] = 0;
        if (Dot(normal, normal) < .0001f) return false;
    }
    if (handle == 4) plane = Add(centre, Forward(original), scale);
    if (!Intersect(eye, firstRay, plane, normal, first) || !Intersect(eye, ray, plane, normal, last)) return false;
    auto delta = Add(last, first, -1);
    light = original;
    if (handle == 4)
    {
        const auto direction = Add(Add(plane, delta), centre, -1);
        const float length = std::sqrt(Dot(direction, direction));
        if (length < .0001f) return false;
        light.yaw = std::atan2(-direction[0], -direction[2]) / radians;
        light.pitch = std::clamp(std::asin(std::clamp(-direction[1] / length, -1.f, 1.f)) / radians, -89.f, 89.f);
    }
    else
    {
        if (handle > 0) for (int axis = 0; axis < 3; ++axis) if (axis != handle - 1) delta[axis] = 0;
        const auto position = Add(centre, delta);
        for (float v : position) if (!std::isfinite(v)) return false;
        light.x = std::clamp(position[0], -32700.f, 32700.f);
        light.y = std::clamp(position[1], -32700.f, 32700.f);
        light.z = std::clamp(position[2], -32700.f, 32700.f);
    }
    return true;
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
