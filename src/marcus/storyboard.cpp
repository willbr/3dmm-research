#include "storyboard.h"
#include "storyboard_tools.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include "storyboard_native.h"
#include "storyboard_resource.h"
#undef size
ASSERTNAME

namespace
{
void Require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
bool Range(float v, float a, float b) { return std::isfinite(v) && v >= a && v <= b; }
const MarcusBoardTransforms BoardTransforms = {
    [](MMActorMatrix *out, float x, float y, float z, unsigned short yaw, unsigned short pitch, unsigned short roll) {
        BMAT34 matrix;
        BrMatrix34RotateZ(&matrix, roll);
        BrMatrix34PostRotateX(&matrix, pitch);
        BrMatrix34PostRotateY(&matrix, yaw);
        matrix.m[3][0] = BrFloatToScalar(x);
        matrix.m[3][1] = BrFloatToScalar(y);
        matrix.m[3][2] = BrFloatToScalar(z);
        static_assert(sizeof(matrix) == sizeof(*out));
        std::memcpy(out, &matrix, sizeof(matrix));
    },
    [](MMActorMatrix *out, const MMActorMatrix *in) -> int {
        BMAT34 matrix, inverse;
        std::memcpy(&matrix, in, sizeof(matrix));
        if (!BrMatrix34Inverse(&inverse, &matrix)) return false;
        std::memcpy(out, &inverse, sizeof(inverse));
        return true;
    },
    [](MMActorPoint *out, const MMActorPoint *in, const MMActorMatrix *transform) {
        BMAT34 matrix;
        BVEC3 point, result;
        static_assert(sizeof(point) == sizeof(*in));
        std::memcpy(&matrix, transform, sizeof(matrix));
        std::memcpy(&point, in, sizeof(point));
        BrMatrix34ApplyP(&result, &point, &matrix);
        std::memcpy(out, &result, sizeof(result));
    }
};
}

bool MarcusBoardView(const MarcusStoryboard &board, MarcusBoardWire &view)
{
    if (board.angles.size() > MARCUS_BOARD_ANGLE_LIMIT || board.lights.size() > MARCUS_BOARD_LIGHT_LIMIT ||
        board.colours.size() > MARCUS_BOARD_COLOUR_LIMIT)
        return false;
    view.values.angle_count = static_cast<int>(board.angles.size());
    view.values.light_count = static_cast<int>(board.lights.size());
    view.values.colour_count = static_cast<int>(board.colours.size());
    std::copy(board.colours.begin(), board.colours.end(), view.values.colours);
    for (size_t i = 0; i < board.angles.size(); ++i)
    {
        if (board.angles[i].pixels.size() != MARCUS_BOARD_PIXELS) return false;
        view.values.angles[i] = MarcusBoardPoseOf(board.angles[i]);
        view.pixels[i] = board.angles[i].pixels.data();
    }
    for (size_t i = 0; i < board.lights.size(); ++i)
        view.values.lights[i] = MarcusBoardLightValuesOf(board.lights[i]);
    return true;
}

bool MarcusBoardValid(const MarcusStoryboard &board)
{
    MarcusBoardWire view{};
    return MarcusBoardView(board, view) && marcus_board_wire_size(&view) != 0;
}

std::vector<unsigned char> MarcusEncodeBoard(const MarcusStoryboard &board)
{
    MarcusBoardWire view{};
    Require(MarcusBoardView(board, view), "Storyboard settings are out of range.");
    std::vector<unsigned char> output(marcus_board_wire_size(&view));
    Require(marcus_board_encode(&view, output.data(), output.size()), "Storyboard settings are out of range.");
    return output;
}

MarcusStoryboard MarcusDecodeBoard(const std::vector<unsigned char> &bytes)
{
    static const char *const messages[] = {
        nullptr,
        "Unsupported storyboard format.",
        "Invalid storyboard size.",
        "Invalid storyboard paint palette.",
        "Invalid legacy paint colour.",
        "Invalid storyboard settings."
    };
    MarcusBoardWire view;
    const int status = marcus_board_decode(bytes.data(), bytes.size(), &view);
    Require(status == MARCUS_BOARD_WIRE_OK, messages[status]);
    return MarcusBoardFromWire(view);
}

MarcusStoryboard MarcusBoardFromWire(const MarcusBoardWire &view)
{
    MarcusStoryboard board;
    const auto &values = view.values;
    board.colours.assign(values.colours, values.colours + values.colour_count);
    board.angles.resize(values.angle_count);
    board.lights.resize(values.light_count);
    for (int i = 0; i < values.angle_count; ++i)
    {
        auto &angle = board.angles[i];
        const auto &pose = values.angles[i];
        angle.camera.x = pose.camera.x; angle.camera.y = pose.camera.y; angle.camera.z = pose.camera.z;
        angle.camera.yaw = pose.camera.yaw; angle.camera.pitch = pose.camera.pitch;
        angle.roll = pose.roll; angle.fov = pose.fov; angle.nearClip = pose.near_clip; angle.farClip = pose.far_clip;
        std::copy_n(view.pixels[i], MARCUS_BOARD_PIXELS, angle.pixels.begin());
    }
    for (int i = 0; i < values.light_count; ++i)
        board.lights[i] = MarcusBoardLightFromValues(values.lights[i]);
    return board;
}
void MarcusBoardStroke(MarcusBoardAngle &a, int x0, int y0, int x1, int y1, int size, unsigned char colour)
{
    if (a.pixels.size() == MARCUS_BOARD_PIXELS)
        marcus_board_stroke(a.pixels.data(), x0, y0, x1, y1, size, colour);
}
std::vector<std::array<double, 4>> MarcusBoardGridLines(const MarcusBoardAngle &a)
{
    std::vector<std::array<double, 4>> lines;
    if (!Range(a.camera.x, -32700, 32700) || !Range(a.camera.y, -32700, 32700) || !Range(a.camera.z, -32700, 32700) ||
        !Range(a.camera.yaw, -360, 360) || !Range(a.camera.pitch, -90, 90) || !Range(a.roll, -180, 180) ||
        !Range(a.fov, 5, 150) || !Range(a.nearClip, .01f, 100)) return lines;
    constexpr int cells = 100;
    lines.reserve(2 * (2 * cells + 1));
    const double yaw = a.camera.yaw * .01745329252, pitch = a.camera.pitch * .01745329252, roll = a.roll * .01745329252;
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
    const double right[3] = {cy * cr - sy * sp * sr, cp * sr, -sy * cr - cy * sp * sr};
    const double up[3] = {-cy * sr - sy * sp * cr, cp * cr, sy * sr - cy * sp * cr};
    const double back[3] = {sy * cp, sp, cy * cp};
    const double focal = .5 / std::tan(a.fov * .00872664626), aspect = double(MarcusBoardWidth) / MarcusBoardHeight;
    const double centreX = std::round(a.camera.x / 10) * 10, centreZ = std::round(a.camera.z / 10) * 10;
    // Keep the ground guide visible from higher, level cameras too.
    const double step = 5 * std::max(1., std::ceil(std::abs(a.camera.y) / 10.)), extent = step * cells;
    auto cameraPoint = [&](double wx, double wz) {
        const double p[3] = {wx - a.camera.x, -a.camera.y, wz - a.camera.z};
        std::array<double, 3> out = {};
        for (int i = 0; i < 3; ++i) { out[0] += p[i] * right[i]; out[1] += p[i] * up[i]; out[2] -= p[i] * back[i]; }
        return out;
    };
    auto line = [&](double x1, double z1, double x2, double z2) {
        auto p = cameraPoint(x1, z1), q = cameraPoint(x2, z2);
        const double nearClip = std::max(.1, double(a.nearClip));
        if (p[2] < nearClip && q[2] < nearClip) return;
        if (p[2] < nearClip) { const double t = (nearClip - p[2]) / (q[2] - p[2]); for (int i = 0; i < 3; ++i) p[i] += t * (q[i] - p[i]); }
        if (q[2] < nearClip) { const double t = (nearClip - q[2]) / (p[2] - q[2]); for (int i = 0; i < 3; ++i) q[i] += t * (p[i] - q[i]); }
        const double x = .5 + focal * p[0] / (aspect * p[2]), y = .5 - focal * p[1] / p[2];
        const double dx = focal / aspect * (q[0] / q[2] - p[0] / p[2]), dy = focal * (p[1] / p[2] - q[1] / q[2]);
        double first = 0, last = 1;
        auto clip = [&](double direction, double distance) {
            if (direction == 0) return distance >= 0;
            const double t = distance / direction;
            if (direction < 0) first = std::max(first, t);
            else last = std::min(last, t);
            return first <= last;
        };
        if (!clip(-dx, x) || !clip(dx, 1 - x) || !clip(-dy, y) || !clip(dy, 1 - y)) return;
        lines.push_back({std::clamp(x + first * dx, 0., 1.), std::clamp(y + first * dy, 0., 1.),
            std::clamp(x + last * dx, 0., 1.), std::clamp(y + last * dy, 0., 1.)});
    };
    for (int i = -cells; i <= cells; ++i)
    {
        line(centreX + i * step, centreZ - extent, centreX + i * step, centreZ + extent);
        line(centreX - extent, centreZ + i * step, centreX + extent, centreZ + i * step);
    }
    return lines;
}
void MarcusBoardBakeGrid(MarcusBoardAngle &a, unsigned char colour)
{
    if (a.pixels.size() != MarcusBoardWidth * MarcusBoardHeight) return;
    MarcusBoardBakeGrid(a, a.pixels.data(), colour);
}

void MarcusBoardBakeGrid(const MarcusBoardAngle &a, unsigned char *pixels, unsigned char colour)
{
    std::vector<unsigned char> mask(MARCUS_BOARD_PIXELS);
    for (const auto &line : MarcusBoardGridLines(a))
        marcus_board_stroke(mask.data(), int(std::lround(line[0] * (MarcusBoardWidth - 1))), int(std::lround(line[1] * (MarcusBoardHeight - 1))),
            int(std::lround(line[2] * (MarcusBoardWidth - 1))), int(std::lround(line[3] * (MarcusBoardHeight - 1))), 1, 1);
    const MarcusBoardGridFade fade(a, MarcusBoardWidth, MarcusBoardHeight);
    for (int y = 0; y < MarcusBoardHeight; ++y) for (int x = 0; x < MarcusBoardWidth; ++x)
    {
        const size_t i = size_t(y) * MarcusBoardWidth + x;
        if (!mask[i]) continue;
        const float opacity = fade.Opacity((x + .5) / MarcusBoardWidth, (y + .5) / MarcusBoardHeight);
        // Indexed paint is opaque. Keep continuous lines and omit the crowded
        // distance, rather than approximating transparency with scattered gaps.
        if (opacity >= .5f) pixels[i] = colour;
    }
}
unsigned char MarcusBoardPaintIndex(const MarcusBoardInk &ink, int x, int y)
{
    return marcus_board_paint_index(&ink, x, y);
}
MarcusBoardInk MarcusBoardMatchColour(const MarcusBoardPalette &palette, unsigned rgb)
{
    return marcus_board_match_colour(palette.data(), rgb);
}
MarcusBoardInks MarcusBoardColourMap(const MarcusBoardPalette &palette, const MarcusBoardPaintColours &colours)
{
    MarcusBoardInks result;
    for (auto rgb : colours) result.push_back(MarcusBoardMatchColour(palette, rgb));
    return result;
}
unsigned MarcusBoardHSV(float hue, float saturation, float value)
{
    if (!std::isfinite(hue) || !std::isfinite(saturation) || !std::isfinite(value)) return 0;
    const float h = std::fmod(std::fmod(hue, 360.f) + 360.f, 360.f) / 60;
    const float s = std::clamp(saturation, 0.f, 1.f), v = std::clamp(value, 0.f, 1.f);
    const float chroma = v * s, x = chroma * (1 - std::abs(std::fmod(h, 2.f) - 1)), m = v - chroma;
    const float channels[6][3] = {{chroma, x, 0}, {x, chroma, 0}, {0, chroma, x}, {0, x, chroma}, {x, 0, chroma}, {chroma, 0, x}};
    unsigned rgb = 0;
    for (int c = 0; c < 3; ++c) rgb = (rgb << 8) | unsigned(std::lround((channels[int(h)][c] + m) * 255));
    return rgb;
}
std::array<float, 3> MarcusBoardToHSV(unsigned rgb)
{
    const float r = ((rgb >> 16) & 255) / 255.f, g = ((rgb >> 8) & 255) / 255.f, b = (rgb & 255) / 255.f;
    const float high = std::max({r, g, b}), low = std::min({r, g, b}), d = high - low;
    float h = 0;
    if (d > 0) h = 60 * (high == r ? (g - b) / d : high == g ? (b - r) / d + 2 : (r - g) / d + 4);
    if (h < 0) h += 360;
    return {h, high > 0 ? d / high : 0, high};
}
int MarcusBoardAddColour(MarcusStoryboard &board, unsigned rgb)
{
    const unsigned char *surfaces[MARCUS_BOARD_ANGLE_LIMIT];
    if (board.angles.size() > MARCUS_BOARD_ANGLE_LIMIT || board.colours.size() > MARCUS_BOARD_COLOUR_LIMIT) return -1;
    for (std::size_t i = 0; i < board.angles.size(); ++i)
    {
        if (board.angles[i].pixels.size() != MARCUS_BOARD_PIXELS) return -1;
        surfaces[i] = board.angles[i].pixels.data();
    }
    const int count = static_cast<int>(board.colours.size());
    const int slot = marcus_board_colour_slot(board.colours.data(), count, rgb, surfaces, static_cast<int>(board.angles.size()));
    if (slot == count) board.colours.push_back(rgb);
    else if (slot >= 0) board.colours[slot] = rgb;
    return slot;
}
MarcusBoardResource MarcusBoardContent(const MarcusStoryboard &board, long source, const std::string &resources, const std::string &content)
{
    auto file = std::make_shared<MarcusMovieFile>(std::filesystem::path{});
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned i = 0; i < 100; ++i)
    {
        auto p = std::filesystem::temp_directory_path() / ("marcus-board-" + std::to_string(stamp) + "-" + std::to_string(i));
        if (std::filesystem::create_directory(p)) { file->directory = p; break; }
    }
    Require(!file->directory.empty(), "Cannot create storyboard working directory.");
    file->path = file->directory / "background.3cn";
    auto chunks = MarcusOpenArchive(file->path, MM_PRIVATE_ARCHIVE_CREATE);
    Require(bool(chunks), "Cannot create storyboard working file.");
    auto resourceFile = MarcusOpenArchive(std::filesystem::u8path(resources), MM_PRIVATE_ARCHIVE_READ);
    Require(bool(resourceFile), "Cannot read the movie palette.");
    auto backgrounds = MarcusOpenArchive(std::filesystem::u8path(content) / "bkgds.3cn", MM_PRIVATE_ARCHIVE_READ);
    Require(bool(backgrounds), "Cannot read the retail black scene.");
    const auto resourceEditor = mm_private_archive_editor(resourceFile.get());
    const auto retailEditor = mm_private_archive_editor(backgrounds.get());
    const auto outputEditor = mm_private_archive_editor(chunks.get());
    MarcusBoardWire view{};
    Require(MarcusBoardView(board, view), "Invalid storyboard.");
    ChunkNumber background;
    const auto error = marcus_board_write_resources(&view, &resourceEditor, &retailEditor, &outputEditor,
                                                    &BoardTransforms, &background);
    Require(!error, error);
    Require(mm_private_archive_save(chunks.get(), kctgSoc), "Cannot save storyboard working file.");
    return {std::make_shared<MarcusStoryboard>(board), file, background, source};
}
void MarcusEmbedBoard(MMPrivateArchive *file, ChunkNumber scene, const MarcusBoardResource &resource)
{
    auto source = MarcusOpenArchive(resource.file->path, MM_PRIVATE_ARCHIVE_READ);
    ChunkNumber background;
    Require(source && mm_private_archive_copy(source.get(), file, kctgBkgd, resource.background, 0, &background),
            "Cannot embed storyboard resources.");
    const auto bytes = MarcusEncodeBoard(*resource.board);
    const auto editor = mm_private_archive_editor(file);
    Require(mm_movie_archive_embed_storyboard(&editor, scene, background, bytes.data(), static_cast<MMU32>(bytes.size())),
            "Cannot embed storyboard data.");
}
MarcusStoryboards MarcusReadBoards(MM::Archive &archive, const std::shared_ptr<MarcusMovieFile> &file)
{
    MarcusStoryboards result;
    for (const auto &scene : archive.Chunks())
    {
        if (scene.id.type != 0x5343454e) continue;
        const MM::Child *metadata = nullptr, *background = nullptr;
        for (const auto &child : scene.children)
        {
            if (child.slot == 0 && child.target.type == MarcusBoardChunk) metadata = &child;
            if (child.slot == 0 && child.target.type == 0x424b4744) background = &child;
        }
        if (!metadata) continue;
        Require(metadata->present && background && background->present, "Storyboard resources are missing.");
        const auto &chunks = archive.Chunks();
        const auto found = std::find_if(chunks.begin(), chunks.end(), [&](const auto &c) { return c.id == metadata->target; });
        MM::PayloadPage page; MM::Error error;
        Require(found != chunks.end() && archive.ReadPayload(static_cast<size_t>(found - chunks.begin()), 0, 1024 * 1024, page, error) &&
            page.totalBytes <= 12 * 1024 * 1024, "Cannot read storyboard data.");
        auto bytes = std::move(page.bytes);
        const auto total = page.totalBytes;
        while (bytes.size() < total)
        {
            Require(archive.ReadPayload(static_cast<size_t>(found - chunks.begin()), static_cast<unsigned>(bytes.size()), 1024 * 1024, page, error) &&
                !page.bytes.empty(), "Cannot read storyboard data.");
            bytes.insert(bytes.end(), page.bytes.begin(), page.bytes.end());
        }
        result[scene.id.number] = {std::make_shared<MarcusStoryboard>(MarcusDecodeBoard(bytes)), file, background->target.number, 0};
    }
    return result;
}
