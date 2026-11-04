/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_dialog.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "platform_files.h"
#include "file_browser.h"
#include <thread>
#include "util.h"
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    std::fprintf(stderr, "%s:%d: %s\n", file, static_cast<int>(line), message ? message : "");
    std::_Exit(2);
}
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Filename dialog failed at %d\n", __LINE__);                                          \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

static bool Choose(Filename &filename, const std::string &path, bool open, bool cancel, bool replace)
{
    std::atomic<bool> finished{false};
    std::thread input([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        std::uint64_t generation = 0;
        int stage = 0;
        bool declined = false;
        while (!finished)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                std::fprintf(stderr, "Filename dialog timed out at stage %d\n", stage);
                std::_Exit(3);
            }
            const auto state = KauaiSDL::CurrentDialog();
            if (state.ready)
            {
                if (generation != state.generation)
                {
                    generation = state.generation;
                    stage = 0;
                }
                const KauaiSDL::DialogControl *name = nullptr;
                for (const auto &control : state.controls)
                    if (control.id == 11 && control.kind == KauaiDialogs::ItemKind::editText)
                        name = &control;
                const auto send = [&](KauaiSDL::DialogActionKind kind, unsigned id, const std::string &text = "") {
                    CHECK(KauaiSDL::QueueDialogAction(generation, {kind, id, text}));
                    ++stage;
                };
                if (name)
                {
                    if (stage == 0 && (cancel || declined))
                        send(KauaiSDL::DialogActionKind::click, 2);
                    else if (stage == 0)
                        send(KauaiSDL::DialogActionKind::text, 11, path);
                    else if (stage == 1 && !cancel && !declined && name->text == path)
                        send(KauaiSDL::DialogActionKind::click, 1);
                }
                else if (stage == 0)
                {
                    CHECK(state.title == "Save As");
                    send(KauaiSDL::DialogActionKind::click, replace ? 1 : 3);
                    declined = !replace;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    achar filters[] = "Movies\0*.3mm\0All files\0*.*\0\0";
    const bool accepted = open ? filename.FGetOpen(filters, nullptr) : filename.FGetSave(filters, nullptr);
    finished = true;
    input.join();
    return accepted;
}

int main()
{
    const auto driver = std::getenv("SDL_VIDEODRIVER");
    if (!driver || std::strcmp(driver, "dummy"))
        return 77; // This test must never open desktop windows.
    char temporary[1024];
    CHECK(KauaiFiles::TempPath(temporary, sizeof(temporary)));
    const auto root = std::string(temporary) + "/kauai-dialog-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    CHECK(KauaiFiles::MakeDirectory(root.c_str()));
    const auto displayExisting = root + "/caf\xC3\xA9.3mm", fresh = root + "/new.3mm";
    std::string existing;
    CHECK(KauaiFiles::NativePathFromUtf8(displayExisting, existing));
    const char original[] = "original movie bytes";
    KauaiFiles::OpenOptions options;
    options.write = true;
    options.creation = KauaiFiles::Creation::CreateExclusive;
    auto file = KauaiFiles::Open(existing.c_str(), options);
    CHECK(file && KauaiFiles::Write(file, original, sizeof(original)));
    KauaiFiles::Close(file);
    Filename filename;
    CHECK(Choose(filename, displayExisting, true, false, false));
    String path;
    filename.GetStnPath(&path);
    char resolved[1024];
    CHECK(KauaiFiles::FullPath(existing.c_str(), resolved, sizeof(resolved)) && KauaiFiles::EqualPaths(resolved, path.Psz()));
    CHECK(!Choose(filename, displayExisting, true, true, false));
    CHECK(filename.Ftg() == ftgNil);
    CHECK(Choose(filename, fresh, false, false, false));
    filename.GetStnPath(&path);
    KauaiFiles::PathInfo info;
    CHECK(KauaiFiles::FullPath(fresh.c_str(), resolved, sizeof(resolved)) && KauaiFiles::EqualPaths(resolved, path.Psz()) && KauaiFiles::Info(fresh.c_str(), &info) == KauaiFiles::Status::Missing);
    CHECK(!Choose(filename, displayExisting, false, false, false));
    CHECK(filename.Ftg() == ftgNil);
    CHECK(Choose(filename, displayExisting, false, false, true));
    options = {};
    file = KauaiFiles::Open(existing.c_str(), options);
    char contents[sizeof(original)];
    std::uint32_t bytes;
    CHECK(file && KauaiFiles::Size(file, &bytes) && bytes == sizeof(original));
    CHECK(KauaiFiles::Read(file, contents, sizeof(contents)) && !std::memcmp(contents, original, sizeof(original)));
    KauaiFiles::Close(file);
    CHECK(KauaiFiles::Delete(existing.c_str()) && KauaiFiles::DeleteDirectory(root.c_str()));
    std::puts(
        "Filename SDL dialogs: UTF-8 open, cancel, new save, overwrite refusal/acceptance; no file writes passed.");
}
