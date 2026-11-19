#include "storyboard.h"
#include "storyboard_tools.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include "storyboard_native.h"
#include "movie_archive_host.h"
#include "zbmp.h"
#undef size
ASSERTNAME

namespace
{
struct Release { template<class T> void operator()(T *p) const { if (p) p->Release(); } };
void Require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
bool Range(float v, float a, float b) { return std::isfinite(v) && v >= a && v <= b; }
unsigned short Angle(float degrees) { return static_cast<unsigned short>(static_cast<int>(degrees * (65536.f / 360))); }
BMAT34 Matrix(float x, float y, float z, float yaw, float pitch, float roll)
{
    BMAT34 m;
    BrMatrix34RotateZ(&m, Angle(roll));
    BrMatrix34PostRotateX(&m, Angle(-pitch));
    BrMatrix34PostRotateY(&m, Angle(yaw));
    m.m[3][0] = BrFloatToScalar(x); m.m[3][1] = BrFloatToScalar(y); m.m[3][2] = BrFloatToScalar(z);
    return m;
}
}
namespace
{
bool BoardView(const MarcusStoryboard &board, MarcusBoardWire &view)
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
}

bool MarcusBoardValid(const MarcusStoryboard &board)
{
    MarcusBoardWire view{};
    return BoardView(board, view) && marcus_board_wire_size(&view) != 0;
}

std::vector<unsigned char> MarcusEncodeBoard(const MarcusStoryboard &board)
{
    MarcusBoardWire view{};
    Require(BoardView(board, view), "Storyboard settings are out of range.");
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
    auto mask = a;
    std::fill(mask.pixels.begin(), mask.pixels.end(), 0);
    for (const auto &line : MarcusBoardGridLines(a))
        MarcusBoardStroke(mask, int(std::lround(line[0] * (MarcusBoardWidth - 1))), int(std::lround(line[1] * (MarcusBoardHeight - 1))),
            int(std::lround(line[2] * (MarcusBoardWidth - 1))), int(std::lround(line[3] * (MarcusBoardHeight - 1))), 1, 1);
    const MarcusBoardGridFade fade(a, MarcusBoardWidth, MarcusBoardHeight);
    for (int y = 0; y < MarcusBoardHeight; ++y) for (int x = 0; x < MarcusBoardWidth; ++x)
    {
        const size_t i = size_t(y) * MarcusBoardWidth + x;
        if (!mask.pixels[i]) continue;
        const float opacity = fade.Opacity((x + .5) / MarcusBoardWidth, (y + .5) / MarcusBoardHeight);
        // Indexed paint is opaque. Keep continuous lines and omit the crowded
        // distance, rather than approximating transparency with scattered gaps.
        if (opacity >= .5f) a.pixels[i] = colour;
    }
}
unsigned char MarcusBoardPaintIndex(const MarcusBoardInk &ink, int x, int y)
{
    static const int pattern[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return pattern[(y & 3) * 4 + (x & 3)] < ink.amount ? ink.second : ink.first;
}
MarcusBoardInk MarcusBoardMatchColour(const MarcusBoardPalette &palette, unsigned rgb)
{
    MarcusBoardInk result;
    double best = 1e20;
    for (unsigned index = 10; index < 246; ++index)
    {
        unsigned distance = 0;
        for (int shift : {0, 8, 16})
        {
            const int difference = int((palette[index] >> shift) & 255) - int((rgb >> shift) & 255);
            distance += difference * difference;
        }
        if (distance < best)
        { best = distance; result = {static_cast<unsigned char>(index), static_cast<unsigned char>(index), 0, palette[index]}; }
    }
    // Ordered mixtures retain soft hues that a single palette entry loses.
    // Penalize contrast to favour nearby colours over noisy complementary pairs.
    for (unsigned a = 10; a < 246 && best > 0; ++a)
        for (unsigned b = a + 1; b < 246; ++b)
        {
            int difference[3], target[3], length = 0, projection = 0;
            for (int c = 0; c < 3; ++c)
            {
                const int shift = c * 8, first = (palette[a] >> shift) & 255;
                difference[c] = int((palette[b] >> shift) & 255) - first;
                target[c] = int((rgb >> shift) & 255) - first;
                length += difference[c] * difference[c]; projection += target[c] * difference[c];
            }
            if (!length || projection <= 0 || projection >= length) continue;
            const int amount = int(std::lround(double(projection) * 16 / length));
            if (amount <= 0 || amount >= 16) continue;
            double score = .01 * length * amount * (16 - amount) / 256.;
            unsigned mixedRgb = 0;
            for (int c = 0; c < 3; ++c)
            {
                const int shift = c * 8, first = (palette[a] >> shift) & 255;
                const int mixed = (first * 16 + difference[c] * amount + 8) / 16;
                const int error = mixed - int((rgb >> shift) & 255);
                score += error * error; mixedRgb |= unsigned(mixed) << shift;
            }
            if (score < best)
            { best = score; result = {static_cast<unsigned char>(a), static_cast<unsigned char>(b), static_cast<unsigned char>(amount), mixedRgb}; }
        }
    return result;
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
static ChunkNumber WriteBoard(PChunkyFile file, const MarcusStoryboard &board, const Color *palette,
    CameraPosition reference, const std::vector<APOS> &referencePlaces)
{
    Require(MarcusBoardValid(board), "Invalid storyboard.");
    BackgroundFile header = {kboCur, koskCur, 10, 0, 0};
    ChunkNumber background, number;
    DataBlock block;
    Require(file->FAddPv(&header, sizeof(header), kctgBkgd, &background), "Cannot write storyboard background.");
    String name; name.SetSzs(PszLit("Storyboard"));
    Require(file->FSetName(kctgBkgd, background, &name), "Cannot name storyboard background.");
    std::unique_ptr<DynamicArray, Release> colours(DynamicArray::PglNew(sizeof(Color)));
    Require(bool(colours), "Cannot allocate storyboard colours.");
    MarcusBoardPalette rgbPalette = {};
    for (unsigned index = 10; index < 246; ++index)
    {
        auto colour = palette[index];
        rgbPalette[index] = (unsigned(colour.bRed) << 16) | (unsigned(colour.bGreen) << 8) | colour.bBlue;
        Require(colours->FAdd(&colour), "Cannot add storyboard colour.");
    }
    const auto paint = MarcusBoardColourMap(rgbPalette, board.colours);
    Require(file->FAddChild(kctgBkgd, background, 0, colours->CbOnFile(), kctgColorTable, &number, &block) && colours->FWrite(&block), "Cannot write storyboard colours.");
    std::unique_ptr<DynamicArray, Release> lights(DynamicArray::PglNew(sizeof(LightPosition)));
    Require(bool(lights), "Cannot allocate storyboard lights.");
    for (const auto &l : board.lights)
    {
        if (l.intensity == 0) continue;
        // Retail's rIntensity is BRender's constant attenuation divisor.
        const float attenuation = std::min(32700.f, 1.f / l.intensity);
        LightPosition light = {Matrix(l.x, l.y, l.z, l.yaw, l.pitch, 0), BrFloatToScalar(attenuation), l.point ? BR_LIGHT_POINT : BR_LIGHT_DIRECT};
        Require(lights->FAdd(&light), "Cannot add storyboard light.");
    }
    Require(file->FAddChild(kctgBkgd, background, 0, lights->CbOnFile(), kctgGllt, &number, &block) && lights->FWrite(&block), "Cannot write storyboard lights.");
    std::unique_ptr<ZBMP, Release> depth(ZBMP::PzbmpNew(MarcusBoardWidth, MarcusBoardHeight));
    Require(bool(depth), "Cannot allocate storyboard depth.");
    std::memset(depth->Prgb(), 255, depth->CbRow() * MarcusBoardHeight);
    ChunkNumber z;
    Require(depth->FWrite(file, kctgZbmp, &z), "Cannot write empty storyboard depth.");
    BMAT34 inverse;
    Require(BrMatrix34Inverse(&inverse, &reference.bmat34Cam) != 0, "Invalid black scene camera transform.");
    std::vector<BVEC3> placements(referencePlaces.size());
    for (size_t i = 0; i < placements.size(); ++i)
    {
        auto point = referencePlaces[i].bvec3Actor;
        BrMatrix34ApplyP(&placements[i], &point, &inverse);
        Require(placements[i].v[2] < 0, "Invalid black scene insertion depth.");
    }
    for (unsigned index = 0; index < board.angles.size(); ++index)
    {
        const auto &a = board.angles[index];
        CameraPosition camera = {};
        camera.bo = kboCur; camera.osk = koskCur;
        camera.zrHither = BrFloatToScalar(a.nearClip); camera.zrYon = BrFloatToScalar(a.farClip); camera.aFov = Angle(a.fov);
        camera.bmat34Cam = Matrix(a.camera.x, a.camera.y, a.camera.z, a.camera.yaw, a.camera.pitch, a.roll);
        // Retain all retail insertion anchors in camera space, adapted to each lens.
        const float lens = std::tan(a.fov * .00872664626f) / std::tan(reference.aFov * (3.14159265358979323846f / 65536));
        std::vector<APOS> points(placements.size());
        for (size_t i = 0; i < placements.size(); ++i)
        {
            const auto &placement = placements[i];
            const float originalDistance = -BrScalarToFloat(placement.v[2]);
            const float distance = std::clamp(originalDistance, a.nearClip + (a.farClip - a.nearClip) * .001f,
                a.farClip - (a.farClip - a.nearClip) * .001f);
            const float scale = lens * distance / originalDistance;
            BVEC3 local = {{BrFloatToScalar(BrScalarToFloat(placement.v[0]) * scale),
                BrFloatToScalar(BrScalarToFloat(placement.v[1]) * scale), BrFloatToScalar(-distance)}};
            BrMatrix34ApplyP(&points[i].bvec3Actor, &local, &camera.bmat34Cam);
        }
        camera.apos = points[0];
        std::vector<unsigned char> cameraBytes(sizeof(camera) + (points.size() - 1) * sizeof(APOS));
        std::memcpy(cameraBytes.data(), &camera, sizeof(camera));
        if (points.size() > 1) std::memcpy(cameraBytes.data() + sizeof(camera), points.data() + 1, (points.size() - 1) * sizeof(APOS));
        ChunkNumber cam, image;
        Require(file->FAddPv(cameraBytes.data(), cameraBytes.size(), kctgCam, &cam) && file->FAdoptChild(kctgBkgd, background, kctgCam, cam, index), "Cannot write storyboard camera.");
        auto pixels = a.pixels;
        for (size_t p = 0; p < pixels.size(); ++p)
            pixels[p] = MarcusBoardPaintIndex(paint[pixels[p]], int(p % MarcusBoardWidth), int(p / MarcusBoardWidth));
        RC rectangle(0, 0, MarcusBoardWidth, MarcusBoardHeight);
        std::unique_ptr<MaskedBitmapMBMP, Release> bitmap(MaskedBitmapMBMP::PmbmpNew(pixels.data(), MarcusBoardWidth, MarcusBoardHeight, &rectangle, 0, 0, 255));
        Require(bitmap && file->FAddChild(kctgCam, cam, 0, bitmap->CbOnFile(), kctgMbmp, &image, &block) && bitmap->FWrite(&block) &&
            file->FAdoptChild(kctgCam, cam, kctgZbmp, z, 0), "Cannot write storyboard painting.");
    }
    return background;
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
    String path; Filename filename;
    path.SetSzs(const_cast<char *>(file->path.u8string().c_str()));
    Require(filename.FBuildFromPath(&path), "Invalid storyboard working path.");
    std::unique_ptr<ChunkyFile, Release> chunks(ChunkyFile::PcflCreate(&filename, fcflWriteEnable));
    Require(bool(chunks), "Cannot create storyboard working file.");
    String resourcePath; Filename resourceFilename;
    resourcePath.SetSzs(const_cast<char *>(resources.c_str()));
    Require(resourceFilename.FBuildFromPath(&resourcePath), "Invalid storyboard resource path.");
    std::unique_ptr<ChunkyFile, Release> resourceFile(ChunkyFile::PcflOpen(&resourceFilename, fcflNil));
    DataBlock block;
    Require(resourceFile && resourceFile->FFind(kctgColorTable, 0, &block), "Cannot read the movie palette.");
    std::unique_ptr<DynamicArray, Release> palette(DynamicArray::PglRead(&block));
    Require(palette && palette->CbEntry() == sizeof(Color) && palette->IvMac() >= 256, "Invalid movie palette.");
    // Preserve the retail black scene's shared actor/background palette. The
    // resource CHK palette is the UI palette, not the scene colour table.
    String contentPath; Filename contentFilename;
    contentPath.SetSzs(const_cast<char *>((std::filesystem::u8path(content) / "bkgds.3cn").u8string().c_str()));
    Require(contentFilename.FBuildFromPath(&contentPath), "Invalid retail background path.");
    std::unique_ptr<ChunkyFile, Release> backgrounds(ChunkyFile::PcflOpen(&contentFilename, fcflNil));
    BackgroundFile black = {}; ChildChunkIdentification child;
    Require(backgrounds && backgrounds->FFind(kctgBkgd, 11, &block) && block.FReadRgb(&black, sizeof(black), 0) &&
        backgrounds->FGetKidChidCtg(kctgBkgd, 11, 0, kctgColorTable, &child) && backgrounds->FFind(kctgColorTable, child.cki.cno, &block),
        "Cannot read the retail black scene palette.");
    std::unique_ptr<DynamicArray, Release> blackColours(DynamicArray::PglRead(&block));
    Require(black.bIndexBase == 10 && blackColours && blackColours->CbEntry() == sizeof(Color) && blackColours->IvMac() == 236,
        "Invalid retail black scene palette.");
    for (LegacyLong i = 0; i < blackColours->IvMac(); ++i) palette->Put(i + black.bIndexBase, blackColours->QvGet(i));
    CameraPosition reference = {};
    Require(backgrounds->FGetKidChidCtg(kctgBkgd, 11, 0, kctgCam, &child) && backgrounds->FFind(kctgCam, child.cki.cno, &block) &&
        block.FUnpackData() && block.FReadRgb(&reference, sizeof(reference), 0), "Cannot read the black scene's insertion camera.");
    Require((block.Cb() - sizeof(reference)) % sizeof(APOS) == 0, "Invalid black scene insertion point list.");
    std::vector<APOS> referencePlaces = {reference.apos};
    for (LegacyLong offset = sizeof(reference); offset < block.Cb(); offset += sizeof(APOS))
    {
        APOS point;
        Require(block.FReadRgb(&point, sizeof(point), offset), "Cannot read an extra black scene insertion point.");
        referencePlaces.push_back(point);
    }
    const auto background = WriteBoard(chunks.get(), board, static_cast<const Color *>(palette->QvGet(0)), reference, referencePlaces);
    // Retail/v3dmm enumerate this child while collecting a background's tags.
    // Keep a valid base-content sound reference, muted and absent from the timeline.
    unsigned char sound[28] = {};
    Require(backgrounds->FGetKidChidCtg(kctgBkgd, 11, 0, kctgBds, &child) && backgrounds->FFind(kctgBds, child.cki.cno, &block) &&
        block.FReadRgb(sound, sizeof(sound), 0), "Cannot read the black scene's default sound.");
    std::memset(sound + 4, 0, 8); // volume and loop flag, in either byte order
    ChunkNumber defaultSound;
    Require(chunks->FAddPv(sound, sizeof(sound), kctgBds, &defaultSound) &&
        chunks->FAdoptChild(kctgBkgd, background, kctgBds, defaultSound, 0), "Cannot write storyboard default sound.");
    Require(chunks->FSave(kctgSoc), "Cannot save storyboard working file.");
    return {std::make_shared<MarcusStoryboard>(board), file, background, source};
}
void MarcusEmbedBoard(PChunkyFile file, ChunkNumber scene, const MarcusBoardResource &resource)
{
    String path; Filename filename;
    path.SetSzs(const_cast<char *>(resource.file->path.u8string().c_str()));
    Require(filename.FBuildFromPath(&path), "Invalid storyboard source path.");
    std::unique_ptr<ChunkyFile, Release> source(ChunkyFile::PcflOpen(&filename, fcflNil));
    ChunkNumber background;
    Require(source && source->FCopy(kctgBkgd, resource.background, file, &background), "Cannot embed storyboard resources.");
    const auto bytes = MarcusEncodeBoard(*resource.board);
    MovieArchiveHost host{file, {}, {}};
    const auto editor = host.Editor();
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
