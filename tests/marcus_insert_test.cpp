// Headless insertion/native round-trip checks; no application window or UI input.
#include "player.h"
#include "storyboard.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <cmath>
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
        MarcusText settings = {};
        require(marcus_text(player.get(), textIndex, &settings) && std::string(settings.text) == words &&
            settings.font == font.chunk && settings.font_source == -1 && settings.shape == 0, "Read newly inserted text settings");
        const auto unchangedDocument = marcus_document(player.get());
        require(marcus_set_text(player.get(), textIndex, &settings) && marcus_document(player.get()) == unchangedDocument,
            "Saving identical text settings makes no edit");
        auto invalid = settings; invalid.shape = 10;
        require(!marcus_set_text(player.get(), textIndex, &invalid) && marcus_document(player.get()) == unchangedDocument,
            "Invalid text edit leaves the object unchanged");
        auto liveEdit = settings; std::strcpy(liveEdit.text, "Live edit"); liveEdit.shape = 1;
        require(marcus_set_text(player.get(), textIndex, &liveEdit) && marcus_text(player.get(), textIndex, &settings) &&
            std::string(settings.text) == "Live edit" && settings.shape == 1, "Edit a newly inserted text in place");
        MarcusObject liveObject = {};
        require(marcus_object(player.get(), textIndex, &liveObject) && liveObject.chunk == textObject.chunk &&
            liveObject.actor_id == textObject.actor_id && liveObject.first_frame == textObject.first_frame,
            "Editing preserves the existing object's identity and entrance");
        require(marcus_undo(player.get()) && marcus_text(player.get(), textIndex, &settings) && std::string(settings.text) == words,
            "Undo restores the new text's original template");
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
        int editIndex = -1;
        for (int i = 0; i < after.object_count; ++i)
        {
            MarcusObject object = {};
            if (marcus_object(player.get(), i, &object) && std::string(object.name) == words) editIndex = i;
        }
        require(editIndex >= 0 && marcus_text(player.get(), editIndex, &settings), "Read saved text's editable settings");
        require(std::string(settings.text) == words && settings.shape == 0 && settings.font == font.chunk,
            "Saved text restores wording, font and shape");
        MarcusObject original = {}; MarcusObjectShape shape = {};
        require(marcus_object(player.get(), editIndex, &original) && marcus_object_shape(player.get(), editIndex, &shape, nullptr, nullptr),
            "Read imported text placement and transform");
        shape.scale[0] = 1.25f; shape.scale[1] = .85f;
        require(marcus_set_object_shape(player.get(), editIndex, &shape), "Resize text before editing");
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image), "Render text before editing");
        const std::vector<unsigned char> beforeEdit(image.rgba, image.rgba + image.pitch * image.height);
        auto editedText = settings; std::strcpy(editedText.text, "Edited"); editedText.font = otherFont.chunk; editedText.shape = 1;
        require(marcus_set_text(player.get(), editIndex, &editedText), "Edit reopened text wording, font and shape");
        MarcusObject updated = {}; MarcusObjectShape updatedShape = {};
        require(marcus_object(player.get(), editIndex, &updated) && updated.actor_id == original.actor_id &&
            updated.chunk == original.chunk && updated.first_frame == original.first_frame && updated.last_frame == original.last_frame &&
            updated.action == original.action && updated.cel == original.cel && updated.x == original.x && updated.y == original.y && updated.z == original.z &&
            std::string(updated.name) == "Edited", "Text edit retains placement, identity and animation");
        require(marcus_object_shape(player.get(), editIndex, &updatedShape, nullptr, nullptr) &&
            std::memcmp(&shape, &updatedShape, sizeof(shape)) == 0, "Text edit retains rotation and scale");
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image), "Render edited text");
        const std::vector<unsigned char> editedPixels(image.rgba, image.rgba + image.pitch * image.height);
        require(beforeEdit != editedPixels, "Editing changes the visible lettering");
        const auto editedPath = (std::filesystem::u8path(argv[3]).parent_path() / "edited-text.3mm").u8string();
        require(marcus_save_copy(player.get(), editedPath.c_str()), "Save edited text and transforms");
        require(marcus_undo(player.get()) && marcus_text(player.get(), editIndex, &settings) && std::string(settings.text) == words &&
            settings.shape == 0 && settings.font == font.chunk, "One undo restores wording, font and shape");
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image) &&
            beforeEdit == std::vector<unsigned char>(image.rgba, image.rgba + image.pitch * image.height), "Text undo restores exact pixels");
        require(marcus_open(player.get(), editedPath.c_str(), argv[1], argv[2]) && marcus_seek(player.get(), 2), "Reopen edited text");
        require(marcus_info(player.get(), &after) && after.object_count == 3, "Editing never inserts an extra object");
        bool editedFound = false, otherUnchanged = false;
        for (int i = 0; i < after.object_count; ++i)
        {
            MarcusObject object = {};
            require(marcus_object(player.get(), i, &object), "Read saved text edit");
            if (std::string(object.name) == "Edited")
            {
                require(marcus_text(player.get(), i, &settings) && std::string(settings.text) == "Edited" &&
                    settings.shape == 1 && settings.font == otherFont.chunk, "Native save retains edited text settings");
                editedFound = true;
            }
            if (std::string(object.name) == "Arc") otherUnchanged = true;
        }
        require(editedFound && otherUnchanged && marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image) &&
            editedPixels == std::vector<unsigned char>(image.rgba, image.rgba + image.pitch * image.height),
            "Edited text native round trip preserves exact pixels and other objects");
        std::puts("Actor and 3D text insertion, validation, native save/reopen, movement and undo passed.");
        return 0;
    }
    catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
