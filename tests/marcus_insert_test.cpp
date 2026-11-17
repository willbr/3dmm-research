// Headless insertion/native round-trip checks; no application window or UI input.
#include "player.h"
#include "storyboard.h"
#include "preview_worker.h"
#include "text_preview.h"
#include <SDL3/SDL.h>
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
        const int materialCount = marcus_material_count(player.get(), argv[2]);
        require(materialCount > 0, "List native materials");
        MarcusTextMaterial pattern = {}, plain = {};
        bool hasPattern = false, hasPlain = false;
        for (int i = 0; i < materialCount && (!hasPattern || !hasPlain); ++i)
        {
            MarcusProp entry = {}; MarcusTextMaterial material = {};
            require(marcus_material(player.get(), i, &entry) && marcus_text_material_preset(player.get(), entry.chunk, &material), "Read native material preset");
            if (material.textured) { pattern = material; hasPattern = true; }
            else { plain = material; hasPlain = true; }
        }
        require(hasPattern && hasPlain, "Native catalogue has solid colours and patterns");
        editIndex = -1;
        for (int i = 0; i < after.object_count; ++i)
        {
            MarcusObject object = {}; marcus_object(player.get(), i, &object);
            if (std::string(object.name) == "Edited") editIndex = i;
        }
        require(editIndex >= 0 && marcus_text(player.get(), editIndex, &settings), "Read text material");
        auto appearanceEdit = settings; appearanceEdit.appearance = pattern;
        auto &material = appearanceEdit.appearance;
        material.ambient = .22f; material.diffuse = .7f; material.specular = .15f; material.shininess = 37;
        material.mapping[0] = .8f; material.mapping[1] = .6f; material.mapping[2] = -.6f; material.mapping[3] = .8f;
        material.mapping[4] = .125f; material.mapping[5] = -.25f;
        require(marcus_set_text(player.get(), editIndex, &appearanceEdit), "Apply text lighting and transformed pattern");
        MarcusText readAppearance = {};
        auto appearanceMatches = [&](const MarcusTextMaterial &value) {
            if (!value.textured || std::abs(value.ambient - material.ambient) > .00002f ||
                std::abs(value.diffuse - material.diffuse) > .00002f || std::abs(value.specular - material.specular) > .00002f ||
                std::abs(value.shininess - material.shininess) > .00002f) return false;
            for (int i = 0; i < 6; ++i) if (std::abs(value.mapping[i] - material.mapping[i]) > .00002f) return false;
            return true;
        };
        require(marcus_text(player.get(), editIndex, &readAppearance) && appearanceMatches(readAppearance.appearance), "Material properties match native precision");
        const auto materialDocument = marcus_document(player.get());
        require(marcus_set_text(player.get(), editIndex, &readAppearance) && marcus_document(player.get()) == materialDocument, "Unchanged appearance is a no-op");
        auto invalidMaterial = readAppearance; invalidMaterial.appearance.ambient = 2;
        require(!marcus_set_text(player.get(), editIndex, &invalidMaterial) && marcus_document(player.get()) == materialDocument, "Reject invalid material without changing the movie");
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image), "Render textured text");
        const std::vector<unsigned char> materialPixels(image.rgba, image.rgba + image.pitch * image.height);
        require(materialPixels != editedPixels, "Changing appearance changes the rendered word");
        const auto materialPath = (std::filesystem::u8path(argv[3]).parent_path() / "material-text.3mm").u8string();
        require(marcus_save_copy(player.get(), materialPath.c_str()), "Save material and texture placement");
        require(marcus_undo(player.get()) && marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image) &&
            editedPixels == std::vector<unsigned char>(image.rgba, image.rgba + image.pitch * image.height), "Undo restores the original material pixels");
        require(marcus_open(player.get(), materialPath.c_str(), argv[1], argv[2]) && marcus_seek(player.get(), 2), "Reopen text appearance");
        for (int i = 0; i < 3; ++i)
        {
            MarcusObject object = {}; marcus_object(player.get(), i, &object);
            if (std::string(object.name) == "Edited") editIndex = i;
        }
        require(marcus_text(player.get(), editIndex, &settings) && appearanceMatches(settings.appearance) &&
            marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image) && materialPixels ==
            std::vector<unsigned char>(image.rgba, image.rgba + image.pitch * image.height), "Native material round trip preserves properties and exact pixels");
        const auto previewDocument = marcus_document(player.get());
        auto previewFile = MarcusPrepareTextPreview(player.get(), editIndex, settings);
        require(bool(previewFile) && marcus_document(player.get()) == previewDocument, "Prepare preview without changing the movie");
        const auto helper = (std::filesystem::u8path(SDL_GetBasePath()) / "marcus-preview.exe").u8string();
        const auto previewPath = previewFile->path.u8string();
        const char *previewArgs[] = {helper.c_str(), previewPath.c_str(), argv[1], "--text", "20 -10 1", nullptr};
        SDL_Process *process = SDL_CreateProcess(previewArgs, true);
        require(process != nullptr, "Launch isolated text preview");
        size_t previewBytes = 0; int previewStatus = -1;
        void *output = SDL_ReadProcess(process, &previewBytes, &previewStatus);
        SDL_DestroyProcess(process);
        require(output && previewStatus == 0, "Read isolated text preview");
        std::vector<std::uint8_t> previewData(static_cast<std::uint8_t *>(output), static_cast<std::uint8_t *>(output) + previewBytes);
        SDL_free(output);
        const auto preview = ReadPreview(previewData);
        require(preview.error.empty() && preview.width == 480 && preview.height == 360 && !preview.rgba.empty(), "Text preview renders existing font and material independently");
        // Exercise the C worker boundary without creating a dialog or UI context.
        auto textPreview = std::unique_ptr<MarcusTextPreview, decltype(&text_preview_destroy)>(
            text_preview_create(), text_preview_destroy);
        require(textPreview && text_preview_start(textPreview.get(), player.get(), editIndex, &settings, argv[1],
            -90, 20, 2), "Start a cancellable C text preview");
        text_preview_stop(textPreview.get());
        MarcusImage previewImage = {};
        require(text_preview_poll(textPreview.get(), &previewImage) == 0, "Cancelled preview does not publish an image");
        require(text_preview_start(textPreview.get(), player.get(), editIndex, &settings, argv[1], 20, -10, 1),
            "Restart C text preview after cancellation");
        const auto previewDeadline = SDL_GetTicks() + 20000;
        int previewReady = 0;
        while (!(previewReady = text_preview_poll(textPreview.get(), &previewImage)) && SDL_GetTicks() < previewDeadline)
            SDL_Delay(5);
        require(previewReady == 1 && previewImage.width == preview.width && previewImage.height == preview.height &&
            previewImage.pitch == preview.width * 4 &&
            std::memcmp(previewImage.rgba, preview.rgba.data(), preview.rgba.size()) == 0 &&
            marcus_document(player.get()) == previewDocument, "C preview preserves exact pixels and the live document");
        require(text_preview_poll(textPreview.get(), &previewImage) == 0, "C preview publishes a completed image only once");
        const auto angled = RunPreviewWorker(previewPath, argv[1], [] { return false; }, "-25 10 1.5",
            false, {}, false, false, true);
        require(angled.error.empty() && angled.rgba != preview.rgba, "Reference orbit produces a different view");
        const auto waitPreview = [&] {
            const auto deadline = SDL_GetTicks() + 20000;
            int ready = 0;
            while (!(ready = text_preview_poll(textPreview.get(), &previewImage)) && SDL_GetTicks() < deadline)
                SDL_Delay(1);
            return ready;
        };
        // Warm camera requests reuse the renderer and template; no UI is created.
        const auto warmStart = SDL_GetTicksNS();
        for (int i = 0; i < 20; ++i)
        {
            const bool alternate = (i % 2) == 0;
            require(text_preview_view(textPreview.get(), alternate ? -25.f : 20.f, alternate ? 10.f : -10.f,
                alternate ? 1.5f : 1.f), "Queue a camera request on the existing text session");
            const auto &expected = alternate ? angled : preview;
            require(waitPreview() == 1 && std::memcmp(previewImage.rgba, expected.rgba.data(), expected.rgba.size()) == 0,
                "Persistent camera views retain exact one-shot pixels, including reset");
        }
        const double warmSeconds = (SDL_GetTicksNS() - warmStart) / 1000000000.0;
        std::printf("Persistent text preview: 20 warm views in %.3f s (%.1f views/s).\n", warmSeconds, 20 / warmSeconds);
        for (int i = 0; i < 100; ++i) text_preview_view(textPreview.get(), float(i), 0, 1);
        require(text_preview_view(textPreview.get(), -25, 10, 1.5f), "Coalesce a burst to its final camera request");
        bool finalView = false;
        const auto burstDeadline = SDL_GetTicks() + 20000;
        while (!finalView && SDL_GetTicks() < burstDeadline)
        {
            const int ready = text_preview_poll(textPreview.get(), &previewImage);
            require(ready >= 0, "Coalesced camera transport remains valid");
            finalView = ready == 1 && std::memcmp(previewImage.rgba, angled.rgba.data(), angled.rgba.size()) == 0;
            if (!finalView) SDL_Delay(1);
        }
        require(finalView, "The final camera in a burst is eventually rendered");
        require(text_preview_view(textPreview.get(), 0, 0, 0) && waitPreview() == -1 && text_preview_error(textPreview.get())[0],
            "Invalid camera receives a bounded failure response");
        require(text_preview_view(textPreview.get(), 20, -10, 1) && waitPreview() == 1 &&
            std::memcmp(previewImage.rgba, preview.rgba.data(), preview.rgba.size()) == 0,
            "A camera error does not destroy the loaded renderer");
        require(text_preview_view(textPreview.get(), -25, 10, 1.5f) &&
            text_preview_start(textPreview.get(), player.get(), editIndex, &settings, argv[1], 20, -10, 1) &&
            waitPreview() == 1 && std::memcmp(previewImage.rgba, preview.rgba.data(), preview.rgba.size()) == 0 &&
            marcus_document(player.get()) == previewDocument, "Replacing a session rejects stale in-flight images");
        text_preview_stop(textPreview.get());
        text_preview_stop(textPreview.get());
        require(text_preview_poll(textPreview.get(), &previewImage) == 0 && !text_preview_view(textPreview.get(), 0, 0, 1),
            "Stopped preview publishes no stale image and rejects camera requests");
        auto wording = settings; std::strcpy(wording.text, "Pattern");
        require(marcus_set_text(player.get(), editIndex, &wording) && marcus_text(player.get(), editIndex, &readAppearance) &&
            appearanceMatches(readAppearance.appearance), "Changing wording retains an imported patterned material");
        require(marcus_undo(player.get()), "Undo wording change");
        auto restyled = settings; restyled.appearance = plain;
        const auto restyledPath = (std::filesystem::u8path(argv[3]).parent_path() / "restyled-text.3mm").u8string();
        require(marcus_set_text(player.get(), editIndex, &restyled) && marcus_save_copy(player.get(), restyledPath.c_str()),
            "Replace a material embedded by an earlier save");
        require(marcus_open(player.get(), restyledPath.c_str(), argv[1], argv[2]) && marcus_seek(player.get(), 2) &&
            marcus_text(player.get(), editIndex, &readAppearance) && !readAppearance.appearance.textured &&
            readAppearance.appearance.index_base == plain.index_base, "Replacement material survives native reopen");
        auto styledNew = settings; std::strcpy(styledNew.text, "Styled"); styledNew.appearance = plain;
        const int styled = marcus_add_text_options(player.get(), &styledNew);
        require(styled >= 0 && marcus_text(player.get(), styled, &readAppearance) && !readAppearance.appearance.textured,
            "Create text with a chosen solid material");
        require(marcus_undo(player.get()) && marcus_info(player.get(), &after) && after.object_count == 3,
            "Creating styled text is one undo step");
        const auto styledPath = (std::filesystem::u8path(argv[3]).parent_path() / "styled-new-text.3mm").u8string();
        require(marcus_add_text_options(player.get(), &styledNew) >= 0 && marcus_save_copy(player.get(), styledPath.c_str()) &&
            marcus_open(player.get(), styledPath.c_str(), argv[1], argv[2]) && marcus_seek(player.get(), 2) &&
            marcus_info(player.get(), &after) && after.object_count == 4, "Save and reopen styled new text");
        bool styledFound = false;
        for (int i = 0; i < after.object_count; ++i)
        {
            MarcusObject object = {}; marcus_object(player.get(), i, &object);
            if (std::string(object.name) != "Styled") continue;
            require(marcus_text(player.get(), i, &readAppearance) && !readAppearance.appearance.textured &&
                readAppearance.appearance.index_base == plain.index_base, "New text retains its chosen native material");
            styledFound = true;
        }
        require(styledFound, "Find the newly styled text after reopen");
        std::puts("Actor and 3D text insertion, validation, native save/reopen, movement and undo passed.");
        return 0;
    }
    catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
