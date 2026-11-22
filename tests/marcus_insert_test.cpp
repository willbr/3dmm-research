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
        using Clipboard = std::unique_ptr<MarcusObjectClipboard, decltype(&marcus_object_clipboard_destroy)>;
        const auto copyRevision = marcus_document(player.get());
        Clipboard actorCopy(marcus_copy_object(player.get(), actorIndex), marcus_object_clipboard_destroy);
        require(actorCopy && marcus_document(player.get()) == copyRevision, "Copy actor without editing the movie");
        const int duplicate = marcus_paste_object(player.get(), actorCopy.get());
        MarcusObject pasted = {};
        require(duplicate >= 0 && marcus_object(player.get(), duplicate, &pasted) && pasted.visible &&
            pasted.actor_id != actorObject.actor_id && pasted.template_chunk == actorObject.template_chunk &&
            pasted.x == actorObject.x && pasted.y == actorObject.y && pasted.z == actorObject.z,
            "Paste an independent actor with the same placement and template");
        require(marcus_undo(player.get()) && marcus_info(player.get(), &after) && after.object_count == 1,
            "Undo removes only the pasted actor");
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
        MarcusObject textBefore = {}, textAfter = {};
        auto animatedText = readAppearance;
        std::strcpy(animatedText.text, "Animated");
        require(marcus_set_text(player.get(), editIndex, &animatedText) &&
            marcus_object(player.get(), editIndex, &textBefore) &&
            marcus_tweak_object_path(player.get(), editIndex, 1, 0, 0) &&
            marcus_object(player.get(), editIndex, &textAfter) && textAfter.actor_id == textBefore.actor_id &&
            std::abs(textAfter.x - textBefore.x - 1) < .001f &&
            marcus_text(player.get(), editIndex, &readAppearance) && appearanceMatches(readAppearance.appearance) &&
            std::strcmp(readAppearance.text, animatedText.text) == 0,
            "Committing a text path edit retains its pending wording and transformed pattern");
        require(marcus_undo(player.get()) && marcus_object(player.get(), editIndex, &textAfter) &&
            textAfter.x == textBefore.x && marcus_text(player.get(), editIndex, &readAppearance) &&
            appearanceMatches(readAppearance.appearance) && std::strcmp(readAppearance.text, animatedText.text) == 0,
            "Undo the path edit while retaining its pending appearance");
        require(marcus_undo(player.get()) && marcus_text(player.get(), editIndex, &readAppearance) &&
            std::strcmp(readAppearance.text, appearanceEdit.text) == 0 && appearanceMatches(readAppearance.appearance),
            "Undo the wording change while retaining the original pattern edit");
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
        Clipboard textCopy(marcus_copy_object(player.get(), editIndex), marcus_object_clipboard_destroy);
        require(bool(textCopy), "Copy textured 3D text");
        player.reset(marcus_create());
        require(marcus_add_scene(player.get(), 11, argv[1], argv[2]), "Open a new destination movie");
        const int pastedText = marcus_paste_object(player.get(), textCopy.get());
        MarcusText pastedSettings = {};
        require(pastedText >= 0 && marcus_object(player.get(), pastedText, &pasted) && pasted.visible && pasted.first_frame == 1 &&
            marcus_text(player.get(), pastedText, &pastedSettings) && std::string(pastedSettings.text) == "Edited" &&
            pastedSettings.font == otherFont.chunk && appearanceMatches(pastedSettings.appearance),
            "Paste rebases the entrance and keeps the embedded text, font and material");
        const auto clipboardPath = (std::filesystem::u8path(argv[3]).parent_path() / "clipboard-text.3mm").u8string();
        require(marcus_save_copy(player.get(), clipboardPath.c_str()) &&
            marcus_open(player.get(), clipboardPath.c_str(), argv[1], argv[2]) &&
            marcus_text(player.get(), 0, &pastedSettings) && appearanceMatches(pastedSettings.appearance),
            "Pasted text preserves its material after native save and reopen");
        require(marcus_open(player.get(), materialPath.c_str(), argv[1], argv[2]) && marcus_seek(player.get(), 2),
            "Return to the source movie after paste");
        const auto previewDocument = marcus_document(player.get());
        auto previewFile = MarcusPrepareTextPreview(player.get(), editIndex, settings);
        require(bool(previewFile) && marcus_document(player.get()) == previewDocument, "Prepare preview without changing the movie");
        const auto previewPath = previewFile->path.u8string();
        const auto preview = RunPreviewWorker(previewPath, argv[1], [] { return false; }, "20 -10 1",
            false, {}, false, false, true);
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
        // Gallery samples use native font/shape/material variants, never the live object.
        for (int kind = 0; kind < 3; ++kind)
        {
            auto sample = settings;
            std::strcpy(sample.text, kind == 0 ? "Once upon a time..." : kind == 1 ? "Caf\xc3\xa9" : "My words");
            sample.shape = kind == 1 ? 3 : 0;
            sample.appearance = kind == 2 ? pattern : plain;
            if (kind == 0) { sample.font = otherFont.chunk; sample.font_source = -1; }
            auto sampleFile = MarcusPrepareTextPreview(player.get(), -1, sample);
            require(bool(sampleFile), "Prepare a private gallery sample");
            const auto reference = RunPreviewWorker(sampleFile->path.u8string(), argv[1], [] { return false; },
                "20 -10 1", false, {}, false, false, true);
            require(reference.error.empty() && !reference.rgba.empty() &&
                text_preview_start(textPreview.get(), player.get(), -1, &sample, argv[1], 20, -10, 1) &&
                waitPreview() == 1 && std::memcmp(previewImage.rgba, reference.rgba.data(), reference.rgba.size()) == 0 &&
                marcus_document(player.get()) == previewDocument,
                "Persistent gallery template loads preserve exact font/shape/material pixels and document identity");
        }
        auto detailedSample = settings;
        std::strcpy(detailedSample.text, "The quick brown fox jumped over the lazy dog.");
        require(!text_preview_start_sized(textPreview.get(), player.get(), -1, &detailedSample, argv[1], 20, -10, 1, 0, 720) &&
            !text_preview_start_sized(textPreview.get(), player.get(), -1, &detailedSample, argv[1], 20, -10, 1, 1025, 720),
            "Detailed preview sizes remain within the bounded transport limits");
        require(text_preview_start_sized(textPreview.get(), player.get(), -1, &detailedSample, argv[1], 20, -10, 1, 960, 720) &&
            waitPreview() == 1 && previewImage.width == 960 && previewImage.height == 720 && previewImage.pitch == 960 * 4,
            "Gallery renders provide native high-resolution pixels for long wording");
        size_t covered = 0;
        for (int y = 0; y < previewImage.height; ++y)
            for (int x = 0; x < previewImage.width; ++x)
                if (previewImage.rgba[y * previewImage.pitch + x * 4 + 3]) ++covered;
        require(covered > 0 && covered < size_t(960 * 720) / 2 && marcus_document(player.get()) == previewDocument,
            "Detailed previews retain transparent trim margins without changing the movie");
        require(text_preview_view(textPreview.get(), -25, 10, 1.5f) && waitPreview() == 1 &&
            previewImage.width == 960 && previewImage.height == 720,
            "Camera updates retain the session's chosen resolution");
        require(text_preview_start(textPreview.get(), player.get(), editIndex, &settings, argv[1], 20, -10, 1) &&
            waitPreview() == 1 && previewImage.width == 480 && previewImage.height == 360 &&
            std::memcmp(previewImage.rgba, preview.rgba.data(), preview.rgba.size()) == 0,
            "Returning to ordinary previews preserves their original pixels and dimensions");
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
        int animated = -1;
        for (int i = 0; i < after.object_count; ++i)
        {
            MarcusObject value = {};
            if (marcus_object(player.get(), i, &value) && value.actor_id == actorObject.actor_id) animated = i;
        }
        MarcusObject atStart = {}, atEdit = {};
        require(animated >= 0 && marcus_object(player.get(), animated, &atStart), "Find actor to animate");
        const int actionCount = marcus_object_action_count(player.get(), animated);
        MarcusObjectAction action = {};
        int chosen = -1;
        for (int i = 0; i < actionCount; ++i)
            if (i != atStart.action && marcus_object_action(player.get(), i, &action) && action.cels > 1)
            { chosen = i; break; }
        require(chosen >= 0 && action.name[0], "List the selected actor's actions and cels");
        const auto actionPreviewDocument = marcus_document(player.get());
        MarcusImage actionPreview = {};
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image), "Render the scene before previewing an action");
        const std::vector<unsigned char> scenePixels(image.rgba, image.rgba + image.pitch * image.height);
        require(marcus_action_preview(player.get(), animated, chosen, 0, 1, 25, -10, 1, 160, 180, &actionPreview),
            "Render the first cel of an isolated action preview");
        const auto firstProjection = actionPreview.projection;
        const std::vector<unsigned char> firstCel(actionPreview.rgba, actionPreview.rgba + actionPreview.pitch * actionPreview.height);
        require(marcus_action_preview(player.get(), animated, chosen, 1, 1, 25, -10, 1, 160, 180, &actionPreview) &&
            std::memcmp(firstCel.data(), actionPreview.rgba, firstCel.size()) != 0,
            "Advancing the preview changes the rendered pose");
        for (int i = 0; i < 12; ++i)
            require(std::abs(firstProjection.transform[i] - actionPreview.projection.transform[i]) < .0001f,
                "Action cels share stable preview framing");
        require(marcus_render(player.get(), 320, 180, 1, 0, 0, 1, &image) &&
            std::memcmp(scenePixels.data(), image.rgba, scenePixels.size()) == 0 &&
            marcus_document(player.get()) == actionPreviewDocument && marcus_info(player.get(), &after) && after.frame == 2,
            "The isolated action preview leaves the scene image and playhead unchanged");
        require(!marcus_set_object_action(player.get(), animated, chosen, action.cels, 0),
                "Reject a starting cel beyond the action");
        require(marcus_set_object_action(player.get(), animated, chosen, 0, 0) &&
            marcus_add_frame(player.get(), 1) && marcus_object(player.get(), animated, &atEdit) &&
            atEdit.action == chosen && atEdit.cel == 1 && atEdit.x == atStart.x,
            "A non-frozen action progresses in place after the end of the path");
        require(marcus_undo(player.get()) && marcus_seek(player.get(), 2), "Undo the added frame");
        require(marcus_set_object_action(player.get(), animated, chosen, 1, 1) &&
            marcus_object(player.get(), animated, &atEdit) && atEdit.action == chosen && atEdit.cel == 1,
            "Set an action and frozen cel at the playhead");
        require(marcus_seek(player.get(), 1) && marcus_object(player.get(), animated, &atEdit) &&
            atEdit.action == atStart.action, "An action edit leaves earlier frames alone");
        MarcusObjectShape animatedShape = {};
        MarcusBounds beforeTweak = {}, afterTweak = {};
        require(marcus_seek(player.get(), 2) &&
            marcus_object_shape(player.get(), animated, &animatedShape, nullptr, nullptr),
            "Read the actor's editable shape");
        animatedShape.scale[0] = 1.5f;
        require(marcus_set_object_shape(player.get(), animated, &animatedShape) &&
            marcus_object_bounds(player.get(), animated, &beforeTweak) && beforeTweak.valid,
            "Resize the actor before editing the native path");
        require(marcus_tweak_object_path(player.get(), animated, 2, 0, 0) &&
            marcus_object(player.get(), animated, &atEdit) && std::abs(atEdit.x - atStart.x - 2) < .001f,
            "Tweak the path position at the current frame");
        require(marcus_object_bounds(player.get(), animated, &afterTweak) && afterTweak.valid,
            "Read the bounds after the path edit");
        for (int i = 0; i < 24; ++i)
            require(std::abs(afterTweak.corners[i] - beforeTweak.corners[i] - (i % 3 == 0 ? 2.f : 0.f)) < .01f,
                "The path edit retains pending size edits");
        require(marcus_undo(player.get()) && marcus_object(player.get(), animated, &atEdit) && atEdit.x == atStart.x,
            "Undo restores the actor's path position");
        const MarcusPathPoint points[] = {{20, 0, 0}, {40, 0, 0}, {60, 0, 10}};
        require(marcus_record_object_path(player.get(), animated, points, 3, 1) &&
            marcus_info(player.get(), &after) && after.last_frame > 2,
            "Record a native path and extend the scene");
        const auto endpoint = after.last_frame;
        require(marcus_seek(player.get(), endpoint) && marcus_object(player.get(), animated, &atEdit) &&
            atEdit.x > atStart.x + 10, "The recorded path moves the actor over time");
        const auto animationPath = (std::filesystem::u8path(argv[3]).parent_path() / "animated-actor.3mm").u8string();
        require(marcus_save_copy(player.get(), animationPath.c_str()) &&
            marcus_open(player.get(), animationPath.c_str(), argv[1], argv[2]) && marcus_seek(player.get(), endpoint),
            "Save and reopen the edited native action and path");
        MarcusObject reopenedPath = {};
        require(marcus_object(player.get(), animated, &reopenedPath) && reopenedPath.actor_id == atStart.actor_id &&
            reopenedPath.x == atEdit.x && reopenedPath.y == atEdit.y && reopenedPath.z == atEdit.z &&
            reopenedPath.action == chosen, "Reopened action and route retain object identity, position and action");
        const MarcusPathPoint detour[] = {{0, 4, 0}, {4, 4, 0}};
        require(marcus_seek(player.get(), 25) && marcus_record_object_path(player.get(), animated, detour, 2, 0) &&
            marcus_info(player.get(), &after) && after.last_frame > endpoint &&
            marcus_seek(player.get(), after.last_frame) && marcus_object(player.get(), animated, &atEdit) &&
            std::abs(atEdit.x - reopenedPath.x) < .01f && std::abs(atEdit.z - reopenedPath.z) < .01f,
            "Rejoin the remaining path and keep its endpoint inside the scene");
        require(marcus_undo(player.get()) && marcus_seek(player.get(), endpoint), "Undo the rejoined path");
        MarcusProp prop = {};
        require(marcus_prop_count(player.get(), argv[2]) > 0 && marcus_prop(player.get(), 0, &prop),
            "Find a prop to animate");
        const int movingProp = marcus_add_prop(player.get(), prop.chunk);
        MarcusObject stationary = {}, movedProp = {};
        const MarcusPathPoint propPoints[] = {{0, 0, 15}, {0, 0, 40}};
        require(movingProp >= 0 && marcus_object(player.get(), movingProp, &stationary) &&
            marcus_record_object_path(player.get(), movingProp, propPoints, 2, 1) &&
            marcus_info(player.get(), &after) && after.last_frame > endpoint &&
            marcus_seek(player.get(), after.last_frame) && marcus_object(player.get(), movingProp, &movedProp) &&
            movedProp.z > stationary.z + 10, "Record a multi-frame path for a stationary prop");
        require(marcus_undo(player.get()) && marcus_info(player.get(), &after) && after.last_frame == endpoint &&
            marcus_object(player.get(), movingProp, &movedProp) && movedProp.z == stationary.z,
            "Undo removes the recorded path and restores the scene range");
        std::puts("Actor and 3D text insertion, native action and path authoring, save/reopen and undo passed.");
        return 0;
    }
    catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
