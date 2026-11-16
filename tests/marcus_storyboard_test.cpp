// Headless authoring/native round-trip checks. No application window or UI input.
#include "storyboard.h"
#include "mm/preview.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>

extern bool MarcusRenderLocalWorld(MarcusPlayer *, const MM::PreviewView &, MM::PreviewImage &, MM::Error &);

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    auto player = std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)>(marcus_create(), marcus_destroy);
    auto check = [&](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(std::string(message) + ": " + marcus_error(player.get()));
    };
    try
    {
        MarcusStoryboard board;
        check(MarcusBoardValid(board), "Default storyboard");
        for (float pitch : {-90.f, 90.f})
        {
            auto vertical = board; vertical.angles[0].camera.pitch = pitch;
            check(MarcusBoardValid(vertical) && MarcusDecodeBoard(MarcusEncodeBoard(vertical)).angles[0].camera.pitch == pitch,
                "Full Fly pitch range survives storyboard serialization");
            const auto verticalGrid = MarcusBoardGridLines(vertical.angles[0]);
            check(std::all_of(verticalGrid.begin(), verticalGrid.end(), [](const auto &line) {
                return std::all_of(line.begin(), line.end(), [](double p) { return std::isfinite(p) && p >= 0 && p <= 1; });
            }), "Vertical camera grid remains finite and clipped");
        }
        MarcusBoardStroke(board.angles[0], 0, 0, 543, 305, 8, 3);
        check(board.angles[0].pixels[0] == 3 && board.angles[0].pixels.back() == 3 && board.angles[0].pixels[100] == 0, "Clipped, continuous opaque strokes");
        MarcusBoardStroke(board.angles[0], 270, 152, 270, 152, 16, 0);
        check(board.angles[0].pixels[152 * 544 + 270] == 0, "Canvas grey covers existing marks");
        const auto originalPainting = board.angles[0].pixels;
        const auto grid = MarcusBoardGridLines(board.angles[0]);
        check(!grid.empty() && std::all_of(grid.begin(), grid.end(), [](const auto &line) {
            return std::all_of(line.begin(), line.end(), [](double p) { return std::isfinite(p) && p >= 0 && p <= 1; });
        }), "Perspective grid is clipped to the canvas");
        MarcusBoardBakeGrid(board.angles[0], 6);
        check(std::count(board.angles[0].pixels.begin(), board.angles[0].pixels.end(), 6) > 500 &&
            std::equal(originalPainting.begin(), originalPainting.begin() + 544 * 153, board.angles[0].pixels.begin()),
            "Bake the ground grid while retaining the drawing above the horizon");
        auto rebaked = board.angles[0]; MarcusBoardBakeGrid(rebaked, 6);
        check(rebaked.pixels == board.angles[0].pixels, "Baking the same grid twice is stable");
        auto turned = board.angles[0]; turned.camera.yaw = 37; turned.camera.pitch = 24; turned.roll = -18; turned.fov = 5; turned.nearClip = .01f;
        const auto turnedGrid = MarcusBoardGridLines(turned);
        check(!turnedGrid.empty() && turnedGrid != grid && std::all_of(turnedGrid.begin(), turnedGrid.end(), [](const auto &line) {
            return std::all_of(line.begin(), line.end(), [](double p) { return std::isfinite(p) && p >= 0 && p <= 1; });
        }), "Moved, rolled and zoomed grids remain clipped");
        auto legacy = MarcusEncodeBoard(board);
        legacy.erase(legacy.begin() + 20, legacy.begin() + 24 + board.colours.size() * 4); legacy[0] = 1;
        check(MarcusEncodeBoard(MarcusDecodeBoard(legacy)) == MarcusEncodeBoard(board), "Read version-one storyboards with the original colour indices");
        for (unsigned char grey = 7; grey < MarcusBoardColourCount; ++grey)
        {
            MarcusBoardStroke(board.angles[0], 30 + grey * 20, 40, 30 + grey * 20, 40, 8, grey);
            check(board.angles[0].pixels[40 * MarcusBoardWidth + 30 + grey * 20] == grey, "Paint each additional grey");
        }
        auto versionTwo = MarcusEncodeBoard(board);
        versionTwo.erase(versionTwo.begin() + 20, versionTwo.begin() + 24 + board.colours.size() * 4); versionTwo[0] = 2;
        check(MarcusEncodeBoard(MarcusDecodeBoard(versionTwo)) == MarcusEncodeBoard(board), "Read version-two storyboard greys");
        check(MarcusBoardHSV(0, 1, 1) == 0xff0000 && MarcusBoardHSV(120, 1, 1) == 0x00ff00 &&
            MarcusBoardHSV(240, 1, 1) == 0x0000ff && MarcusBoardHSV(360, 1, 1) == 0xff0000 &&
            MarcusBoardHSV(77, 0, 1) == 0xffffff && MarcusBoardHSV(77, 1, 0) == 0, "HSV primaries, hue wrap, white and black");
        for (auto rgb : {0x2357a5U, 0xffffffU, 0U, 0x949494U, 0x9ebfd9U})
        {
            const auto hsv = MarcusBoardToHSV(rgb);
            check(MarcusBoardHSV(hsv[0], hsv[1], hsv[2]) == rgb, "RGB/HSV round trip keeps the selected paint colour");
        }
        auto fullPalette = board;
        for (unsigned i = MarcusBoardColourCount; i < 256; ++i) fullPalette.colours.push_back(0x123400 + i);
        for (unsigned i = 0; i < 256; ++i) fullPalette.angles[0].pixels[i] = static_cast<unsigned char>(i);
        const auto fullColours = fullPalette.colours;
        check(MarcusBoardAddColour(fullPalette, 0xabcdef) == -1 && fullPalette.colours == fullColours,
            "A full paint palette cannot recolour existing strokes");
        fullPalette.angles[0].pixels[200] = 0;
        check(MarcusBoardAddColour(fullPalette, 0xabcdef) == 200 && fullPalette.colours.size() == 256,
            "An unused custom colour slot can be reused safely");
        const auto customRgb = MarcusBoardHSV(280, .8f, .7f);
        const int customInk = MarcusBoardAddColour(board, customRgb);
        check(customInk == int(MarcusBoardColourCount) && MarcusBoardAddColour(board, customRgb) == customInk &&
            board.colours.size() == MarcusBoardColourCount + 1, "Reuse an existing custom colour without consuming another slot");
        MarcusBoardStroke(board.angles[0], 500, 24, 520, 24, 8, static_cast<unsigned char>(customInk));
        board.angles.emplace_back();
        // Keep this prop/lighting fixture close to the ground, independent of the new-scene camera height.
        board.angles[1].camera.x = 7; board.angles[1].camera.y = 5; board.angles[1].camera.yaw = -25; board.angles[1].camera.pitch = 12;
        board.angles[1].roll = 8; board.angles[1].fov = 62;
        std::fill(board.angles[1].pixels.begin(), board.angles[1].pixels.end(), 5);
        board.lights.push_back({2, 8, 4, 10, -20, .6f, 1});
        const auto bytes = MarcusEncodeBoard(board);
        check(MarcusEncodeBoard(MarcusDecodeBoard(bytes)) == bytes, "Lossless versioned editor data");
        auto broken = bytes; broken.pop_back();
        bool rejected = false;
        try { MarcusDecodeBoard(broken); } catch (const std::exception &) { rejected = true; }
        check(rejected, "Reject truncated editor data");
        auto invalid = board; invalid.angles[0].fov = std::numeric_limits<float>::quiet_NaN();
        check(!MarcusBoardValid(invalid), "Reject nonfinite camera");
        invalid = board; invalid.angles[0].pixels[0] = static_cast<unsigned char>(board.colours.size());
        check(!MarcusBoardValid(invalid), "Reject invalid paint colour");
        invalid = board; invalid.colours.back() = 0x1000000;
        check(!MarcusBoardValid(invalid), "Reject invalid custom RGB values");
        check(MarcusApplyStoryboard(player.get(), board, true, 0, argv[1], argv[2]), "Create blank storyboard movie");
        MarcusInfo info = {};
        check(marcus_info(player.get(), &info) && info.scene_count == 1 && info.camera_count == 2 && info.camera == 0 && info.object_count == 0 && info.sound_count == 0 && info.frame == 1, "Blank scene admission");
        // Viewport tools commit completed gestures to the ordinary movie history.
        auto painted = board;
        for (int stroke = 0; stroke < 3; ++stroke)
        {
            const auto revision = marcus_document(player.get());
            MarcusBoardStroke(painted.angles[0], 400, 30 + stroke * 12, 460, 30 + stroke * 12, 4, 1);
            check(MarcusApplyStoryboard(player.get(), painted, false, 0, argv[1], argv[2]) &&
                marcus_document(player.get()) != revision && marcus_info(player.get(), &info) && info.frame == 1 && info.camera == 0,
                "A completed stroke commits without moving the timeline");
            MarcusStoryboard current;
            check(MarcusGetStoryboard(player.get(), current) && MarcusEncodeBoard(current) == MarcusEncodeBoard(painted),
                "Movie state immediately contains a completed stroke");
        }
        for (int stroke = 0; stroke < 3; ++stroke) check(marcus_undo(player.get()), "Undo individual viewport strokes");
        MarcusStoryboard restored;
        check(MarcusGetStoryboard(player.get(), restored) && MarcusEncodeBoard(restored) == bytes,
            "Movie undo restores the original canvas after multiple strokes");
        MarcusProjection projection;
        check(marcus_camera_projection(player.get(), 1, &projection) && std::abs(projection.transform[9] - 7) < .001 &&
            std::abs(projection.transform[7] - std::sin(12 * .01745329252)) < .001 && std::abs(projection.vertical_fov - 62) < .01, "Native camera pose, pitch convention and FOV");
        MM::PreviewView view; view.width = 544; view.height = 306; view.includeDepth = true; view.renderAtOutputResolution = true;
        MM::PreviewImage image; MM::Error error;
        check(MarcusRenderLocalWorld(player.get(), view, image, error), "Render storyboard image");
        check(image.depth.size() == 544 * 306 && std::all_of(image.depth.begin(), image.depth.end(), [](float d) { return std::isinf(d) && d > 0; }), "Every storyboard depth is infinity");
        check(marcus_add_frame(player.get(), 1) && marcus_set_scene_camera(player.get(), 1), "Author second camera cut");
        check(marcus_seek(player.get(), 1) && marcus_info(player.get(), &info) && info.camera == 0 &&
            marcus_seek(player.get(), 2) && marcus_info(player.get(), &info) && info.camera == 1, "Camera cut survives seeking");
        auto edited = board; edited.angles[1].fov = 70; edited.angles[0].pixels[1000] = 4;
        MarcusBoardBakeGrid(edited.angles[1], 3);
        check(MarcusApplyStoryboard(player.get(), edited, false, 1, argv[1], argv[2]) &&
            marcus_camera_projection(player.get(), 1, &projection) && std::abs(projection.vertical_fov - 70) < .01, "Edit camera and painting");
        check(marcus_undo(player.get()) && marcus_camera_projection(player.get(), 1, &projection) && std::abs(projection.vertical_fov - 62) < .01, "Undo restores native scenery");
        MarcusStoryboard undone;
        check(MarcusGetStoryboard(player.get(), undone) && MarcusEncodeBoard(undone) == bytes, "Undo restores the painting before a grid bake");
        check(marcus_reload(player.get(), argv[1], argv[2]) && marcus_is_storyboard(player.get()), "Reload retains custom backgrounds");
        check(marcus_seek(player.get(), 2), "Return to second cut");
        int props = marcus_prop_count(player.get(), argv[2]);
        check(props > 0, "Prop catalogue available");
        MarcusProp prop = {}; check(marcus_prop(player.get(), 0, &prop), "Read prop");
        for (int i = 0; i < props; ++i)
        {
            MarcusProp candidate = {}; marcus_prop(player.get(), i, &candidate);
            if (std::string(candidate.name).find("Ball") != std::string::npos) { prop = candidate; break; }
        }
        std::printf("Storyboard prop: %s\n", prop.name); std::fflush(stdout);
        check(marcus_add_prop(player.get(), prop.chunk) >= 0, "Block out with 3D props");
        MarcusObject object; check(marcus_object(player.get(), 0, &object), "Read placed object");
        check(MarcusRenderLocalWorld(player.get(), view, image, error) &&
            std::any_of(image.depth.begin(), image.depth.end(), [](float d) { return std::isfinite(d); }), "Props render in front of the painting");
        MarcusImage preview;
        check(MarcusRenderStoryboard(player.get(), board, 1, preview), "Draft camera/light preview");
        std::vector<unsigned char> originalPreview(preview.rgba, preview.rgba + preview.pitch * preview.height);
        auto dark = board; for (auto &l : dark.lights) l.intensity = 0;
        check(MarcusRenderStoryboard(player.get(), dark, 1, preview), "Preview disabled lights");
        const std::vector<unsigned char> darkPreview(preview.rgba, preview.rgba + preview.pitch * preview.height);
        auto bright = board;
        bright.lights = {{0, 0, 0, -25, 12, 1, 0}, {0, 0, 0, 155, -12, 1, 0}};
        check(MarcusRenderStoryboard(player.get(), bright, 1, preview) &&
            !std::equal(darkPreview.begin(), darkPreview.end(), preview.rgba), "Draft lights change actor shading");
        check(MarcusRenderStoryboard(player.get(), board, 1, preview) &&
            std::equal(originalPreview.begin(), originalPreview.end(), preview.rgba), "Temporary lights restore exactly");
        const auto path = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
        std::filesystem::create_directories(path.parent_path());
        auto unsafePath = path; unsafePath.replace_extension(".3mm");
        check(marcus_requires_vmm(player.get()) && !marcus_save_copy(player.get(), unsafePath.u8string().c_str()),
            "Storyboard exports require a VMM, protecting retail movies");
        check(marcus_save_copy(player.get(), path.u8string().c_str()), "Save native movie copy");
        check(marcus_save_copy(player.get(), path.u8string().c_str()), "Atomically replace saved copy");
        const auto bundle = MM::V3dmm::ReadBundle(path);
        check(bundle.dependencies.size() == 2 && bundle.dependencies[0].name == "3dmm" && bundle.dependencies[1].archive.length > 0,
            "VMM carries the base dependency and a complete portable background package");
        const auto package = MM::V3dmm::ReadPackage(bundle.dependencies[1].archive);
        check(package.metadata.at("marcusstoryboard") == "1", "Identify generated background-only packages for replacement");
        const auto packageDirectory = path.parent_path() / ("storyboard-package-check-" + package.shortName);
        std::filesystem::create_directories(packageDirectory);
        const auto members = MM::V3dmm::Materialize(bundle.dependencies[1].archive, packageDirectory);
        auto payload = [&](MM::Archive &archive, MM::Id id) {
            const auto &chunks = archive.Chunks();
            const auto at = std::find_if(chunks.begin(), chunks.end(), [&](const auto &c) { return c.id == id; });
            MM::PayloadPage page; MM::Error readError;
            check(at != chunks.end() && archive.ReadPayload(at - chunks.begin(), 0, 4096, page, readError), "Read packaged palette bytes");
            return page.bytes;
        };
        MM::Archive retail, packaged; MM::Error readError;
        check(retail.Open((std::filesystem::u8path(argv[2]) / "bkgds.3cn").u8string(), readError) &&
            packaged.Open(members.at(package.shortName + ".3cn").u8string(), readError), "Open reference and packaged content");
        const auto blackPalette = payload(retail, {0x474c4352, 0x1000c});
        MarcusBoardPalette colours = {};
        for (unsigned i = 10; i < 246; ++i)
        {
            const auto *p = blackPalette.data() + 12 + (i - 10) * 4;
            colours[i] = (unsigned(p[2]) << 16) | (unsigned(p[1]) << 8) | p[0];
        }
        const auto inks = MarcusBoardColourMap(colours);
        check(inks[1].rgb == 0 && inks[1].amount == 0 && inks[2].rgb == 0xffffff && inks[2].amount == 0,
            "Black and white remain solid palette colours");
        check(((inks[6].rgb >> 8) & 255) > ((inks[6].rgb >> 16) & 255) &&
            ((inks[6].rgb >> 8) & 255) > (inks[6].rgb & 255), "Dither preserves the soft green hue instead of choosing grey");
        for (const auto &ink : inks)
        {
            int second = 0;
            for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
            {
                const auto index = MarcusBoardPaintIndex(ink, x, y);
                check(index >= 10 && index < 246, "Paint does not use reserved palette indices");
                if (ink.first != ink.second && index == ink.second) ++second;
            }
            check(second == ink.amount, "Dither pattern uses the chosen mixture exactly");
        }
        int palettes = 0;
        for (const auto &chunk : packaged.Chunks())
            if (chunk.id.type == 0x474c4352)
            { check(payload(packaged, chunk.id) == blackPalette, "Preserve every retail scene palette entry, including prop colours"); ++palettes; }
        check(palettes > 0, "Packaged background has the standard palette");
        packaged.Close(); retail.Close();
        for (const auto &member : members) std::filesystem::remove(member.second);
        std::filesystem::remove(packageDirectory);
        check(marcus_open(player.get(), path.u8string().c_str(), argv[1], argv[2]), "Reopen native storyboard movie");
        MarcusStoryboard loaded;
        check(MarcusGetStoryboard(player.get(), loaded) && MarcusEncodeBoard(loaded) == bytes, "Reopen editable drawings and lights");
        check(marcus_seek(player.get(), 2) && marcus_info(player.get(), &info) && info.camera == 1 && info.object_count == 1, "Reopen camera cuts and props");
        check(marcus_object(player.get(), 0, &object), "Reopen object");
        edited = loaded; edited.angles[1].camera.x += 2;
        check(MarcusApplyStoryboard(player.get(), edited, false, 1, argv[1], argv[2]) && marcus_object(player.get(), 0, &object), "Edit reopened storyboard without losing actors");
        check(marcus_undo(player.get()) && MarcusGetStoryboard(player.get(), loaded) && MarcusEncodeBoard(loaded) == bytes, "Undo reopened storyboard edit");
        check(!marcus_save_copy(player.get(), path.u8string().c_str()), "Save copy protects its open source");
        auto resavedPath = path; resavedPath.replace_filename(path.stem().u8string() + "-resaved.vmm");
        check(marcus_save_copy(player.get(), resavedPath.u8string().c_str()) &&
            MM::V3dmm::ReadBundle(resavedPath).dependencies.size() == 2, "Resaving replaces stale generated packages");
        for (int renderer = 0; renderer < MARCUS_RENDERER_COUNT; ++renderer)
        {
            if (!marcus_set_renderer(player.get(), renderer)) { std::printf("Renderer %d unavailable: %s\n", renderer, marcus_error(player.get())); continue; }
            MarcusImage frame;
            check(marcus_render(player.get(), 544, 306, 1, 0, 0, 1, &frame), "Render native storyboard backend");
            check(frame.rgba && frame.width > 0 && frame.height > 0, "Native backend returns pixels");
        }
        check(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER), "Restore CPU renderer");
        check(MarcusApplyStoryboard(player.get(), dark, false, 1, argv[1], argv[2]) &&
            MarcusRenderLocalWorld(player.get(), view, image, error), "Disabled native lights render safely");
        check(marcus_undo(player.get()), "Undo disabled lights");
        // Exercise more than one archive payload page, plus mixed-scene snapshots.
        auto many = board; many.angles.resize(8);
        check(MarcusApplyStoryboard(player.get(), many, true, 7, argv[1], argv[2]), "Create multi-page storyboard metadata");
        auto manyPath = path; manyPath.replace_filename(path.stem().u8string() + "-many.vmm");
        check(marcus_save_copy(player.get(), manyPath.u8string().c_str()) && marcus_open(player.get(), manyPath.u8string().c_str(), argv[1], argv[2]) &&
            marcus_select_scene(player.get(), 1) && MarcusGetStoryboard(player.get(), loaded) && loaded.angles.size() == 8, "Reopen paged multi-scene editor data");
        player.reset(); player.reset(marcus_create());
        auto ordinaryPath = path; ordinaryPath.replace_filename("ordinary-black.3mm");
        check(marcus_add_scene(player.get(), 11, argv[1], argv[2]) && !marcus_requires_vmm(player.get()) &&
            marcus_save_copy(player.get(), ordinaryPath.u8string().c_str()), "Ordinary movies retain native 3MM saving");
        auto ordinaryBundle = ordinaryPath; ordinaryBundle.replace_extension(".vmm");
        auto copiedBundle = ordinaryPath; copiedBundle.replace_filename("ordinary-copy.vmm");
        check(marcus_save_copy(player.get(), ordinaryBundle.u8string().c_str()) &&
            marcus_open(player.get(), ordinaryBundle.u8string().c_str(), argv[1], argv[2]) &&
            marcus_save_copy(player.get(), copiedBundle.u8string().c_str()), "Save a VMM without storyboard edits");
        auto extracted = ordinaryPath; extracted.replace_filename("ordinary-copy-inner.3mm");
        MM::V3dmm::Copy(MM::V3dmm::ReadBundle(copiedBundle).movie, extracted);
        MM::Archive ordinary;
        check(ordinary.Open(extracted.u8string(), readError), "Copied VMM contains a native movie rather than another VMM");
        std::puts("Storyboard strokes, validation, no-depth rendering, cameras, props, undo, reload and native save/reopen passed.");
        return 0;
    }
    catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
