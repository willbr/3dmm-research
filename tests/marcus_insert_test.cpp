// Headless insertion/native round-trip checks; no application window or UI input.
#include "player.h"
#include "storyboard.h"
#include <cstdio>
#include <cstring>
#include <memory>
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
    try
    {
        require(marcus_add_scene(player.get(), 11, argv[1], argv[2]), "Create black scene");
        MarcusInfo before = {}, after = {};
        require(marcus_info(player.get(), &before), "Read empty scene");
        const auto document = marcus_document(player.get());
        require(marcus_actor_count(player.get(), argv[2]) > 0 && marcus_font_count(player.get(), argv[2]) > 0,
            "List original actors and 3D fonts");
        require(marcus_document(player.get()) == document, "Listing does not modify the movie");
        MarcusProp actor = {}, font = {};
        require(marcus_actor(player.get(), 0, &actor) && marcus_font(player.get(), 0, &font), "Read catalogue entries");
        require(marcus_add_actor(player.get(), 0xffffffffUL) < 0 && marcus_add_text(player.get(), "", font.chunk, 0) < 0 &&
            marcus_add_text(player.get(), "Text", 0xffffffffUL, 0) < 0 &&
            marcus_add_text(player.get(), std::string(51, 'A').c_str(), font.chunk, 0) < 0 &&
            marcus_add_text(player.get(), "Text", font.chunk, 10) < 0 &&
            marcus_add_text(player.get(), "\xf0\x9f\x98\x80", font.chunk, 0) < 0, "Reject unavailable and unsupported insertions");
        require(marcus_document(player.get()) == document, "Invalid insertions leave the document unchanged");
        MarcusImage image = {};
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image), "Render empty scene");
        const std::vector<unsigned char> empty(image.rgba, image.rgba + image.pitch * image.height);
        const int actorIndex = marcus_add_actor(player.get(), actor.chunk);
        MarcusObject actorObject = {};
        require(actorIndex >= 0 && marcus_object(player.get(), actorIndex, &actorObject) &&
            actorObject.visible && !actorObject.prop && !actorObject.text, "Insert and select actor");
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image) &&
            std::memcmp(empty.data(), image.rgba, empty.size()) != 0, "Inserted actor is visible in the shot");
        require(marcus_add_frame(player.get(), 1), "Append second frame");
        const char *words = "Caf\xc3\xa9";
        const int textIndex = marcus_add_text(player.get(), words, font.chunk, 0);
        MarcusObject textObject = {};
        require(textIndex >= 0 && marcus_object(player.get(), textIndex, &textObject) &&
            textObject.visible && textObject.text && textObject.first_frame == 2, "Insert 3D text at current frame");
        MarcusObject beforeText = {};
        require(marcus_seek(player.get(), 1) && marcus_object(player.get(), textIndex, &beforeText) && !beforeText.visible,
            "Text is off stage before its insertion frame");
        require(marcus_seek(player.get(), 2), "Return to text insertion frame");
        require(marcus_move_object(player.get(), textIndex, textObject.x + 2, textObject.y, textObject.z), "Move new 3D text");
        MarcusProp otherFont = font;
        marcus_font(player.get(), 1, &otherFont);
        require(marcus_add_text(player.get(), "Arc", otherFont.chunk, 7) >= 0, "Insert a second embedded text template");
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image), "Render moved text before save");
        const std::vector<unsigned char> savedPixels(image.rgba, image.rgba + image.pitch * image.height);
        require(marcus_save_copy(player.get(), argv[3]), "Save actor and embedded 3D text");
        require(marcus_undo(player.get()) && marcus_undo(player.get()) && marcus_undo(player.get()) && marcus_info(player.get(), &after) && after.object_count == 1,
            "Undo both text insertions and movement");
        require(marcus_add_text(player.get(), "Again", font.chunk, 7) >= 0, "Insert another text shape after undo");
        require(marcus_open(player.get(), argv[3], argv[1], argv[2]), "Reopen native movie");
        require(marcus_seek(player.get(), 2) && marcus_info(player.get(), &after) && after.object_count == 3,
            "Native movie retains all inserted objects");
        bool foundActor = false, foundText = false, foundOther = false;
        for (int i = 0; i < after.object_count; ++i)
        {
            MarcusObject object = {};
            require(marcus_object(player.get(), i, &object), "Read reopened object");
            if (object.text)
            {
                if (std::string(object.name) == words) foundText = object.visible && object.first_frame == 2;
                if (std::string(object.name) == "Arc") foundOther = object.visible && object.first_frame == 2;
            }
            else foundActor = object.visible && !object.prop;
        }
        require(foundActor && foundText && foundOther, "Native text retains its UTF-8 display name and actor kind");
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image), "Render reopened native text and actor");
        require(savedPixels.size() == std::size_t(image.pitch) * image.height &&
            std::memcmp(savedPixels.data(), image.rgba, savedPixels.size()) == 0, "Native round trip preserves actor and text pixels");
        std::puts("Actor and 3D text insertion, validation, native save/reopen, movement and undo passed.");
        return 0;
    }
    catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
