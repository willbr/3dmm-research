#include "caption_quad.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace ThreeMM;
void Check(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "Caption quad check failed at line %d\n", line);
        std::exit(1);
    }
}
#define CHECK(value) Check((value), __LINE__)
PreviewProjection Camera()
{
    PreviewProjection camera;
    camera.valid = true;
    camera.transform[0] = camera.transform[4] = camera.transform[8] = 1;
    camera.transform[11] = 10;
    camera.verticalFov = 90;
    camera.aspect = 1;
    camera.nearClip = 1;
    camera.farClip = 100;
    camera.displayDepth = 10;
    return camera;
}
PreviewImage Scene()
{
    PreviewImage image;
    image.width = image.height = 32;
    image.projection = Camera();
    image.rgba.resize(32 * 32 * 4);
    image.depth.assign(32 * 32, std::numeric_limits<float>::infinity());
    for (std::size_t i = 0; i < image.rgba.size(); i += 4)
    {
        image.rgba[i] = 10;
        image.rgba[i + 1] = 20;
        image.rgba[i + 2] = 30;
        image.rgba[i + 3] = 255;
    }
    return image;
}
const std::uint8_t *Pixel(const PreviewImage &image, unsigned x, unsigned y)
{
    return &image.rgba[(std::size_t(y) * image.width + x) * 4];
}
int main()
{
    auto camera = Camera();
    PreviewImage texture;
    texture.width = texture.height = 4;
    texture.rgba.resize(4 * 4 * 4);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
        {
            auto pixel = &texture.rgba[(y * 4 + x) * 4];
            pixel[x < 2 ? 0 : 1] = 255;
            pixel[3] = y < 2 ? 255 : x < 2 ? 128 : 0;
        }
    auto scene = Scene();
    const auto original = scene.rgba;
    marcus_caption_quad(texture, camera, scene);
    CHECK(Pixel(scene, 4, 4)[0] == 255 && Pixel(scene, 27, 4)[1] == 255);
    CHECK(Pixel(scene, 4, 27)[0] > 120 && Pixel(scene, 4, 27)[0] < 140);
    CHECK(Pixel(scene, 27, 27)[0] == 10 && Pixel(scene, 27, 27)[2] == 30);
    scene = Scene();
    std::fill(scene.depth.begin(), scene.depth.end(), 5.f);
    marcus_caption_quad(texture, camera, scene);
    CHECK(scene.rgba == original); // Nearer geometry occludes the caption plane.
    std::fill(scene.depth.begin(), scene.depth.end(), 20.f);
    marcus_caption_quad(texture, camera, scene);
    CHECK(scene.rgba != original);
    scene = Scene();
    scene.projection.transform[9] = 5;
    marcus_caption_quad(texture, camera, scene);
    CHECK(Pixel(scene, 31, 4)[0] == 10); // Moving the viewer reveals the quad's edge.
    scene = Scene();
    scene.projection.transform[11] = 20;
    marcus_caption_quad(texture, camera, scene);
    CHECK(Pixel(scene, 0, 0)[0] == 10 && Pixel(scene, 12, 12)[0] == 255);
    scene = Scene();
    scene.projection.transform[11] = -10;
    scene.projection.transform[0] = scene.projection.transform[8] = -1;
    marcus_caption_quad(texture, camera, scene);
    CHECK(Pixel(scene, 4, 4)[1] == 255 && Pixel(scene, 27, 4)[0] == 255); // Back face is visible and mirrored.
    scene = Scene();
    scene.projection.transform[11] = -1;
    marcus_caption_quad(texture, camera, scene);
    CHECK(scene.rgba == original); // Plane behind the viewer.
    scene = Scene();
    scene.projection.nearClip = 11;
    marcus_caption_quad(texture, camera, scene);
    CHECK(scene.rgba == original);
    scene = Scene();
    scene.projection.farClip = 9;
    marcus_caption_quad(texture, camera, scene);
    CHECK(scene.rgba == original);
    scene = Scene();
    scene.projection.transform[0] = scene.projection.transform[8] = 0;
    scene.projection.transform[2] = -1;
    scene.projection.transform[6] = 1;
    scene.projection.transform[11] = 5;
    marcus_caption_quad(texture, camera, scene);
    CHECK(scene.rgba != original); // A plane crossing the viewing near plane still clips safely.
    scene = Scene();
    camera.transform[0] = camera.transform[4] = 0;
    camera.transform[1] = 1;
    camera.transform[3] = -1;
    marcus_caption_quad(texture, camera, scene);
    CHECK(Pixel(scene, 4, 27)[0] == 255); // Authored camera roll rotates the texture with its plane.
    scene = Scene();
    camera.transform[0] = camera.transform[1] = camera.transform[3] = camera.transform[4] = 0;
    marcus_caption_quad(texture, camera, scene);
    CHECK(scene.rgba == original); // Singular authored transform.
    std::puts(
        "Caption quad: perspective, transparency, depth occlusion, roll, back face and near/far clipping passed.");
    return 0;
}
