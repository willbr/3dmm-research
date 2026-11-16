#include "colour_picker.h"
#include "../../vendor/ok-color/ok_color.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float triangleHeight = .8660254037844386f;
float Unit(float v) { return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0; }
float Hue(float v)
{
    if (!std::isfinite(v)) return 0;
    const float wrapped = std::fmod(v, 360.f);
    return wrapped < 0 ? wrapped + 360 : wrapped;
}
unsigned Pack(ok_color::RGB rgb)
{
    auto byte = [](float v) { return unsigned(std::lround(Unit(v) * 255)); };
    return byte(rgb.r) << 16 | byte(rgb.g) << 8 | byte(rgb.b);
}
}

unsigned MarcusOkHSL(float hue, float saturation, float lightness)
{
    return Pack(ok_color::okhsl_to_srgb({Hue(hue) / 360, Unit(saturation), Unit(lightness)}));
}
std::array<float, 3> MarcusToOkHSL(unsigned rgb)
{
    const ok_color::RGB input = {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f};
    // The reference inverse divides by chroma; handle exact neutral colours
    // explicitly so black, white and greys never introduce an undefined hue.
    if (((rgb >> 16) & 255) == ((rgb >> 8) & 255) && ((rgb >> 8) & 255) == (rgb & 255))
    {
        if (rgb == 0) return {0, 0, 0};
        if (rgb == 0xffffff) return {0, 0, 1};
        const auto lab = ok_color::linear_srgb_to_oklab({ok_color::srgb_transfer_function_inv(input.r),
            ok_color::srgb_transfer_function_inv(input.g), ok_color::srgb_transfer_function_inv(input.b)});
        return {0, 0, Unit(ok_color::toe(lab.L))};
    }
    const auto hsl = ok_color::srgb_to_okhsl(input);
    return {Hue(float(hsl.h * 360)), Unit(hsl.s), Unit(hsl.l)};
}
std::array<float, 2> MarcusOkHSLTrianglePoint(float saturation, float lightness)
{
    const float l = Unit(lightness), chromatic = Unit(saturation) * (1 - std::abs(2 * l - 1));
    return {1.5f * chromatic - .5f, (1 - 2 * l) * triangleHeight};
}
bool MarcusOkHSLTriangleContains(float x, float y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    const float chromatic = (x + .5f) / 1.5f;
    return chromatic >= -1e-6f && chromatic <= 1 - std::abs(y / triangleHeight) + 1e-6f;
}
std::array<float, 2> MarcusOkHSLTrianglePick(float x, float y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) return {0, .5f};
    if (!MarcusOkHSLTriangleContains(x, y))
    {
        constexpr std::array<std::array<float, 2>, 3> corners = {{{1, 0}, {-.5f, -triangleHeight}, {-.5f, triangleHeight}}};
        float closest = std::numeric_limits<float>::infinity(), px = 0, py = 0;
        for (int i = 0; i < 3; ++i)
        {
            const auto &a = corners[i], &b = corners[(i + 1) % 3];
            const float dx = b[0] - a[0], dy = b[1] - a[1];
            const float t = std::clamp(((x - a[0]) * dx + (y - a[1]) * dy) / (dx * dx + dy * dy), 0.f, 1.f);
            const float qx = a[0] + t * dx, qy = a[1] + t * dy;
            const float distance = (x - qx) * (x - qx) + (y - qy) * (y - qy);
            if (distance < closest) { closest = distance; px = qx; py = qy; }
        }
        x = px; y = py;
    }
    const float lightness = Unit((1 - y / triangleHeight) / 2);
    const float width = 1 - std::abs(2 * lightness - 1);
    return {width > 1e-6f ? Unit(((x + .5f) / 1.5f) / width) : 0, lightness};
}
std::vector<unsigned char> MarcusOkHSLTrianglePixels(float hue, int side)
{
    if (side <= 0 || side > 1024) return {};
    std::vector<unsigned char> pixels(size_t(side) * side * 4, 0);
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
    {
        const float px = 2 * (x + .5f) / side - 1, py = 2 * (y + .5f) / side - 1;
        if (!MarcusOkHSLTriangleContains(px, py)) continue;
        const auto sl = MarcusOkHSLTrianglePick(px, py);
        const unsigned rgb = MarcusOkHSL(hue, sl[0], sl[1]);
        auto p = pixels.data() + (size_t(y) * side + x) * 4;
        p[0] = static_cast<unsigned char>(rgb >> 16); p[1] = static_cast<unsigned char>(rgb >> 8);
        p[2] = static_cast<unsigned char>(rgb); p[3] = 255;
    }
    return pixels;
}
