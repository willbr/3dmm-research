// Headless costume-event, isolated-preview, undo and native round-trip checks.
#include "player.h"
#include "storyboard.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    auto player = std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)>(marcus_create(), marcus_destroy);
    auto require = [&](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(std::string(message) + ": " + marcus_error(player.get()));
    };
    auto list = [&](int object) {
        const int count = marcus_costume_count(player.get(), object);
        require(count >= 0, "Read costume count");
        std::vector<MarcusCostume> result(count);
        for (int i = 0; i < count; ++i) require(marcus_costume(player.get(), object, i, &result[i]), "Read costume");
        return result;
    };
    auto selected = [&](int object, const MarcusCostume &choice) {
        const auto values = list(object);
        return std::any_of(values.begin(), values.end(), [&](const auto &c) {
            return c.body_set == choice.body_set && c.costume == choice.costume && c.selected;
        });
    };
    auto pixels = [&](int object, const MarcusCostume *choice, float yaw = 25.f, float pitch = -10.f, float zoom = 1.f) {
        MarcusImage image = {};
        require(marcus_costume_preview(player.get(), object, choice, choice ? 1 : 0, yaw, pitch, zoom, 240, 300, &image), "Render isolated costume preview");
        require(image.rgba && image.width == 240 && image.height == 300, "Preview dimensions");
        require(image.projection.valid && image.projection.near_clip > 0 &&
            image.projection.far_clip / image.projection.near_clip < 8,
            "Isolated preview uses a tight depth range for material layers");
        return std::vector<unsigned char>(image.rgba, image.rgba + image.pitch * image.height);
    };
    try
    {
        require(marcus_add_scene(player.get(), 11, argv[1], argv[2]), "Create scene");
        require(marcus_actor_count(player.get(), argv[2]) > 0, "Read actor catalogue");
        MarcusProp actor = {};
        require(marcus_actor(player.get(), 0, &actor), "Read first actor");
        const int object = marcus_add_actor(player.get(), actor.chunk);
        require(object >= 0, "Insert actor");
        for (int f = 1; f < 10; ++f) require(marcus_add_frame(player.get(), 1), "Extend scene");
        require(marcus_seek(player.get(), 1), "Seek actor entrance");
        MarcusObject placed = {};
        require(marcus_object(player.get(), object, &placed) && marcus_move_object(player.get(), object, placed.x + 1, placed.y, placed.z),
            "Move actor before costume editing");
        const auto values = list(object);
        std::printf("%s: %zu costume choices\n", actor.name, values.size());
        require(!values.empty(), "Actor has costumes");
        const auto originalPixels = pixels(object, nullptr);
        require(pixels(object, nullptr, 25, -10, 4) == originalPixels, "Zoom stops at the full-object fit");
        for (int angle = 0; angle < 360; angle += 45)
        {
            const auto rendered = pixels(object, nullptr, float(angle), angle % 90 ? -35.f : 20.f);
            int left = 240, top = 300, right = -1, bottom = -1;
            for (int y = 0; y < 300; ++y) for (int x = 0; x < 240; ++x) if (rendered[(y * 240 + x) * 4 + 3])
            { left = std::min(left, x); right = std::max(right, x); top = std::min(top, y); bottom = std::max(bottom, y); }
            require(left > 1 && top > 1 && right < 238 && bottom < 298 && right > left && bottom > top,
                "Rotated preview fits the complete actor with a visible margin");
            require(bottom - top > 180 || right - left > 144, "Preview uses the available image area");
        }
        MarcusObjectShape shape = {};
        require(marcus_object_shape(player.get(), object, &shape, nullptr, nullptr), "Read actor transform");
        shape.rotation[0] = shape.rotation[8] = 0; shape.rotation[2] = -1; shape.rotation[6] = 1;
        require(marcus_set_object_shape(player.get(), object, &shape), "Turn actor in movie");
        MarcusImage shot = {};
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &shot), "Render turned actor in movie");
        const std::vector<unsigned char> moviePixels(shot.rgba, shot.rgba + shot.pitch * shot.height);
        require(pixels(object, nullptr) == originalPixels, "Costume view faces consistently regardless of movie rotation");
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &shot) && moviePixels ==
            std::vector<unsigned char>(shot.rgba, shot.rgba + shot.pitch * shot.height), "Costume preview restores exact movie pixels");
        MarcusObjectShape afterPreview = {};
        require(marcus_object_shape(player.get(), object, &afterPreview, nullptr, nullptr) &&
            std::equal(shape.rotation, shape.rotation + 9, afterPreview.rotation), "Preview preserves the movie rotation");
        require(marcus_undo(player.get()), "Restore actor rotation");
        const auto document = marcus_document(player.get());
        std::vector<MarcusCostume> current;
        for (const auto &c : values) if (c.selected) current.push_back(c);
        require(marcus_set_costumes(player.get(), object, current.data(), int(current.size())) &&
            marcus_document(player.get()) == document, "Identical costume is a no-op");
        auto invalid = values.front(); invalid.costume = -12345;
        require(!marcus_set_costumes(player.get(), object, &invalid, 1) && marcus_document(player.get()) == document,
            "Reject unavailable costume without an edit");
        MarcusCostume alternative = {}; bool found = false;
        for (const auto &c : values) if (!c.selected && pixels(object, &c) != originalPixels)
        { alternative = c; found = true; break; }
        require(found && pixels(object, nullptr) == originalPixels && marcus_document(player.get()) == document,
            "Variant preview is visible and leaves the live movie unchanged");
        std::map<int, MarcusProjection> focusCameras;
        int focusedImages = 0;
        for (const auto &c : values)
        {
            MarcusImage focused = {};
            require(marcus_costume_part_preview(player.get(), object, c.body_set, &c, 1, 25, -10, 1, 240, 300, &focused),
                "Render changed body-part preview");
            require(focused.content_rect[0] >= 0 && focused.content_rect[1] >= 0 &&
                focused.content_rect[2] > 0 && focused.content_rect[3] > 0 &&
                focused.content_rect[0] + focused.content_rect[2] <= focused.width &&
                focused.content_rect[1] + focused.content_rect[3] <= focused.height &&
                focused.content_rect[2] * focused.content_rect[3] < focused.width * focused.height,
                "Focused previews supply a valid crop around the changing region");
            const std::vector<unsigned char> partPixels(focused.rgba, focused.rgba + focused.pitch * focused.height);
            if (partPixels != pixels(object, &c)) ++focusedImages;
            auto camera = focusCameras.find(c.body_set);
            if (camera == focusCameras.end()) focusCameras[c.body_set] = focused.projection;
            else for (int axis = 0; axis < 12; ++axis)
                require(std::abs(camera->second.transform[axis] - focused.projection.transform[axis]) < .0001f,
                    "Variants in one body-part row retain identical camera framing");
        }
        require(focusedImages > 2 && focusCameras.size() > 2 && pixels(object, nullptr) == originalPixels &&
            marcus_document(player.get()) == document, "Focused clothing and accessory previews preserve the movie");
        MarcusImage unavailable = {};
        require(!marcus_costume_part_preview(player.get(), object, 9999, nullptr, 0, 25, -10, 1, 240, 300, &unavailable) &&
            marcus_document(player.get()) == document, "Unavailable body-part focus is rejected without editing");
        std::vector<MarcusCostume> combined{alternative};
        const auto otherPart = std::find_if(values.begin(), values.end(), [&](const auto &c) { return !c.selected && c.body_set != alternative.body_set; });
        if (otherPart != values.end()) combined.push_back(*otherPart);
        require(marcus_seek(player.get(), 5) && marcus_set_costumes(player.get(), object, combined.data(), int(combined.size())), "Change clothing and accessories at frame five");
        for (const auto &c : combined) require(selected(object, c), "All chosen body parts applied together");
        require(selected(object, alternative), "New costume selected immediately");
        require(marcus_seek(player.get(), 4) && !selected(object, alternative), "Earlier frames retain original costume");
        require(marcus_seek(player.get(), 10) && selected(object, alternative), "Costume persists forward");
        require(marcus_undo(player.get()) && marcus_seek(player.get(), 5) && pixels(object, nullptr) == originalPixels,
            "One undo restores original track and rendered costume");
        require(marcus_set_costumes(player.get(), object, &alternative, 1), "Reapply costume");
        auto original = std::find_if(values.begin(), values.end(), [&](const auto &c) { return c.body_set == alternative.body_set && c.selected; });
        require(original != values.end(), "Find original part costume");
        require(marcus_seek(player.get(), 9) && marcus_set_costumes(player.get(), object, &*original, 1), "Authored later costume");
        auto earlier = alternative;
        const auto third = std::find_if(values.begin(), values.end(), [&](const auto &c) {
            return c.body_set == alternative.body_set && c.costume != alternative.costume && c.costume != original->costume;
        });
        if (third != values.end()) earlier = *third;
        require(marcus_seek(player.get(), 3) && marcus_set_costumes(player.get(), object, &earlier, 1), "Edit before later changes");
        require(marcus_seek(player.get(), 6) && selected(object, alternative) &&
            marcus_seek(player.get(), 9) && selected(object, *original), "Later distinct costume changes are retained");
        require(marcus_prop_count(player.get(), argv[2]) > 0, "Read props");
        int propObject = -1; MarcusCostume propChoice = {};
        for (int i = 0; i < marcus_prop_count(player.get(), argv[2]); ++i)
        {
            MarcusProp prop = {};
            require(marcus_prop(player.get(), i, &prop), "Read prop entry");
            propObject = marcus_add_prop(player.get(), prop.chunk);
            require(propObject >= 0, "Insert prop");
            const auto costumes = list(propObject);
            auto choice = std::find_if(costumes.begin(), costumes.end(), [](const auto &c) { return !c.selected; });
            if (choice != costumes.end()) { propChoice = *choice; std::printf("%s: %zu costume choices\n", prop.name, costumes.size()); break; }
            require(marcus_undo(player.get()), "Remove prop without variants"); propObject = -1;
        }
        require(propObject >= 0 && marcus_set_costumes(player.get(), propObject, &propChoice, 1) && selected(propObject, propChoice), "Change prop costume");
        require(marcus_undo(player.get()) && !selected(propObject, propChoice), "Undo prop costume");
        require(marcus_set_costumes(player.get(), propObject, &propChoice, 1), "Reapply prop costume");
        const auto savedActor = pixels(object, nullptr), savedProp = pixels(propObject, nullptr);
        std::filesystem::create_directories(std::filesystem::u8path(argv[3]).parent_path());
        require(marcus_save_copy(player.get(), argv[3]), "Save native costume events");
        require(marcus_open(player.get(), argv[3], argv[1], argv[2]) && marcus_seek(player.get(), 9), "Reopen native movie");
        require(selected(object, *original) && selected(propObject, propChoice) && pixels(object, nullptr) == savedActor &&
            pixels(propObject, nullptr) == savedProp, "Actor and prop costumes preserve exact pixels after save/reopen");
        require(marcus_seek(player.get(), 1) && selected(object, *original) && marcus_seek(player.get(), 4) && selected(object, earlier) &&
            marcus_seek(player.get(), 6) && selected(object, alternative),
            "Native file preserves costume event times");
        require(marcus_set_costumes(player.get(), object, &*original, 1) && selected(object, *original) &&
            marcus_undo(player.get()) && selected(object, alternative), "Reopened actor tracks support further costume edits and undo");
        std::puts("Costume previews, frame events, undo and native actor/prop round trips passed.");
        return 0;
    }
    catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
