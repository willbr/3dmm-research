/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_packs.h"
#include "pack_hash.h"
#include "iso_cache.h"
#include "three_mm/nick_expansion.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "soc_core.h"
#undef size
ASSERTNAME
namespace fs = std::filesystem;
using namespace StudioPacks;
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%d %s\n", file, int(line), message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(2);
}
void Check(bool value)
{
    if (!value)
        throw std::runtime_error("Pack test failed");
}
template <class F> void Reject(F action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    Check(rejected);
}
void Write(const fs::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
    Check(bool(out));
}
void Fixture(const fs::path &path)
{
    auto absolute = fs::absolute(path).string();
    String text;
    text.SetSzs(absolute.data());
    Filename name;
    Check(name.FBuildFromPath(&text));
    auto file = ChunkyFile::PcflCreate(&name, fcflWriteEnable);
    byte bytes[] = {1, 3, 5, 7};
    Check(file && file->FPutPv(bytes, 4, 'TEST', 1) && file->FSave(kctgSoc));
    ReleasePpo(&file);
}
void Lifecycle(const fs::path &root)
{
    Check(Hash(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Check(Hash("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string million(1000000, 'a');
    Check(Hash(million.data(), million.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    Write(root / "hash.bin", million);
    Check(HashFile(root / "hash.bin") == Hash(million.data(), million.size()));
    Check(Identify("N3DMMExp") == 0 && Identify("DORAEMON") == 1 && Identify("Expansions") == 2 &&
          Identify("unknown") == -1);
    auto preferences = Parse("");
    Check(Serialize(Parse(Serialize(preferences))) == Serialize(preferences));
    Reject([] {
        Parse("{\"nickelodeon\":{\"generation\":\"../../bad\",\"enabled\":true,\"remove\":false,\"source\":3}}");
    });
    Fixture(root / "fixture.3cn");
    Store store(root / "store");
    for (const auto &def : Definitions())
    {
        auto source = root / def.key;
        fs::create_directory(source);
        for (const auto &name : def.files)
            fs::copy_file(root / "fixture.3cn", source / name);
        Write(source / "installer.exe", "unrelated source sentinel");
        auto stage = store.StageFolder(def.id, source);
        auto pending = stage.path;
        Check(fs::exists(pending / "pack.json") && !fs::exists(pending / "installer.exe"));
        store.Discard(stage);
        Check(!fs::exists(pending));
        stage = store.StageFolder(def.id, source);
        auto generation = stage.generation;
        store.Publish(stage);
        Preference pref;
        pref.generation = generation;
        auto info = store.Inspect(def.id, pref, true);
        Check(info.valid);
        auto installed = fs::u8path(info.path);
        Check(HashFile(installed / def.files.front()) == HashFile(source / def.files.front()));
        Write(installed / "my-file.txt", "preserve unknown files");
        {
            std::ofstream changed(installed / def.files.front(), std::ios::binary | std::ios::app);
            changed << "changed";
        }
        Check(!store.Inspect(def.id, pref, true).valid);
        Check(store.Remove(def.id, pref) == 1);
        Check(fs::exists(installed / "my-file.txt") && fs::exists(installed / def.files.front()));
        Check(!fs::exists(installed / def.files.back()) && fs::exists(source / "installer.exe"));
        Check(store.Remove(def.id, pref) == 1); // Retry after manifest removal preserves unknown data.
        stage.generation.clear();
        auto rollback = store.StageFolder(def.id, source);
        pref.generation = rollback.generation;
        store.Publish(rollback);
        auto published = store.Inspect(def.id, pref).path;
        store.Discard(rollback);
        Check(!fs::exists(fs::u8path(published)));
#ifdef _WIN32
        auto lockedStage = store.StageFolder(def.id, source);
        pref.generation = lockedStage.generation;
        store.Publish(lockedStage);
        auto lockedPath = fs::u8path(store.Inspect(def.id, pref).path);
        {
            std::ifstream locked(lockedPath / def.files.front(), std::ios::binary);
            Check(bool(locked));
            Reject([&] { store.Remove(def.id, pref); });
            Check(fs::exists(lockedPath / "pack.json"));
        }
        Check(store.Remove(def.id, pref) == 0 && !fs::exists(lockedPath));
#endif
        Write(source / def.files.front(), "bad archive");
        Reject([&] { store.StageFolder(def.id, source); });
        Check(fs::is_empty(root / "store/.pending"));
    }
}
int main(int argc, char **argv)
{
    try
    {
        if (argc < 2)
            return 2;
        auto root = fs::absolute(argv[argc - 1]);
        root /= "run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(root);
        if (argc == 2)
            Lifecycle(root);
        else if (argc == 7 && std::string(argv[1]) == "--nick")
        {
            Store store(root / "store");
            auto stage = store.StageISO(Id::Nickelodeon, argv[2], argv[3], argv[4]);
            ThreeMM::NickComparison result;
            std::string error;
            Check(ThreeMM::CompareNickExpansion(stage.path.string(), argv[5], result, error));
            Check(!result.imageDifferences && !result.otherDifferences && result.chunks == 11082);
            std::printf("Nickelodeon: %llu chunks, identical decoded pixels and metadata.\n",
                        static_cast<unsigned long long>(result.chunks));
            store.Discard(stage);
            Reject([&] { store.StageISO(Id::Nickelodeon, argv[2], argv[3], argv[2]); }); // Not a fixup ZIP.
        }
        else if (argc == 4 && std::string(argv[1]) == "--dora")
        {
            Store store(root / "store");
            auto stage = store.StageISO(Id::Doraemon, argv[2], {}, {});
            auto originals = StudioISO::ReadFiles(argv[2], "CONTENTS", Definitions()[1].files);
            for (const auto &file : originals)
                Check(HashFile(stage.path / file.first) == Hash(file.second.data(), file.second.size()));
            store.Discard(stage);
            std::puts("Doraemon: all eight imported archives match original disc bytes.");
        }
        else
            return 2;
        std::puts("Pack tests passed.");
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
