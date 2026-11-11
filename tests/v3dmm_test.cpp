// Licensed under the MIT License.
#include "three_mm/v3dmm.h"
#include "studio_vxp_store.h"
#include <miniz.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "soc_core.h"
#undef size
ASSERTNAME
namespace fs = std::filesystem;
namespace V = ThreeMM::V3dmm;
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
        throw std::runtime_error("V3DMM test failed");
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
std::string Read(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void Write(const fs::path &path, const std::string &bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), bytes.size());
    Check(bool(file));
}
void Fixture(const fs::path &path)
{
    auto absolute = fs::absolute(path).string();
    String text;
    text.SetSzs(absolute.data());
    Filename filename;
    Check(filename.FBuildFromPath(&text));
    auto file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
    byte bytes[] = {1, 3, 5, 7};
    Check(file && file->FPutPv(bytes, sizeof(bytes), 'TEST', 0xf1234567) && file->FSave(kctgSoc));
    ReleasePpo(&file);
}
void Zip(const fs::path &path, const fs::path &content, const std::string &extra = {}, const std::string &settings = {},
         const std::string &name = "Synthetic VXP")
{
    auto data = Read(content);
    mz_zip_archive zip{};
    Check(mz_zip_writer_init_heap(&zip, 0, 0) != 0);
    std::string cfg = "Name=" + name + "\r\nAuthor=3DMMForever\r\nType=Portable\r\nUnknown=retained\r\n";
    cfg += settings;
    Check(mz_zip_writer_add_mem(&zip, "fixture.cfg", cfg.data(), cfg.size(), MZ_BEST_COMPRESSION) != 0);
    for (auto name : {"fixture.3cn", "fixture.3th"})
        Check(mz_zip_writer_add_mem(&zip, name, data.data(), data.size(), MZ_BEST_COMPRESSION) != 0);
    if (!extra.empty())
        Check(mz_zip_writer_add_mem(&zip, extra.c_str(), data.data(), data.size(), 0) != 0);
    void *bytes = nullptr;
    std::size_t length = 0;
    Check(mz_zip_writer_finalize_heap_archive(&zip, &bytes, &length) != 0);
    Write(path, std::string(static_cast<char *>(bytes), length));
    mz_free(bytes);
    mz_zip_writer_end(&zip);
}
void DuplicateTests(const fs::path &root)
{
    // Frozen answers from v3dmm 0.14's PhysicsFS 1.0.60 DLL. Winners vary
    // between members in the same ZIP; neither first nor last entry wins.
    struct Expected
    {
        unsigned count, cfg, content, thumbnail;
    };
    for (const auto &expected :
         {Expected{2, 1, 0, 0}, Expected{3, 2, 0, 2}, Expected{6, 5, 4, 2}, Expected{8, 7, 0, 3}})
    {
        mz_zip_archive zip{};
        Check(mz_zip_writer_init_heap(&zip, 0, 0) != 0);
        for (unsigned i = 0; i < expected.count; ++i)
        {
            const auto suffix = std::to_string(i);
            for (const auto &entry : std::vector<std::pair<std::string, std::string>>{
                     {"fixture.cfg", "Name=Choice " + suffix + "\nAuthor=Oracle\nType=Portable\n"},
                     {"fixture.3cn", "content-" + suffix},
                     {"fixture.3th", "thumbnail-" + suffix}})
                Check(mz_zip_writer_add_mem(&zip, entry.first.c_str(), entry.second.data(), entry.second.size(),
                                            MZ_BEST_COMPRESSION) != 0);
        }
        void *bytes = nullptr;
        std::size_t length = 0;
        Check(mz_zip_writer_finalize_heap_archive(&zip, &bytes, &length) != 0);
        const auto archive = root / ("duplicates-" + std::to_string(expected.count) + ".vxp");
        Write(archive, std::string(static_cast<char *>(bytes), length));
        mz_free(bytes);
        mz_zip_writer_end(&zip);
        const auto package = V::ReadPackage(V::WholeFile(archive));
        Check(package.members.size() == 3 && package.name == "Choice " + std::to_string(expected.cfg));
        const auto output = root / ("duplicates-" + std::to_string(expected.count));
        fs::create_directory(output);
        const auto files = V::Materialize(V::WholeFile(archive), output);
        Check(files.size() == 2 && Read(files.at("fixture.3cn")) == "content-" + std::to_string(expected.content));
        Check(Read(files.at("fixture.3th")) == "thumbnail-" + std::to_string(expected.thumbnail));
        // Budgets still count all central-directory records, including losers.
        V::Limits small;
        small.expandedBytes = 100;
        Reject([&] { V::ReadPackage(V::WholeFile(archive), small); });
    }
}
void CatalogTests(const fs::path &root)
{
    fs::create_directories(root);
    Fixture(root / "content.3cn");
    Zip(root / "one.vxp", root / "content.3cn", {}, {}, "One");
    Zip(root / "two.vxp", root / "content.3cn", {}, {}, "Two");
    StudioV3dmm::Store store(root / "store");
    const auto one = store.Install(V::WholeFile(root / "one.vxp"));
    const auto two = store.Install(V::WholeFile(root / "two.vxp"));
    Check(fs::exists(store.Root() / "catalog.sqlite3") && !fs::exists(store.Root() / "catalog.json"));
    // Migrate an old catalog without changing its order, enable flags or files.
    const auto legacy = root / "legacy";
    fs::create_directory(legacy);
    for (const auto &entry : {one, two})
    {
        fs::create_directory(legacy / entry.hash);
        for (const auto &file : fs::directory_iterator(entry.archive.parent_path()))
            fs::copy_file(file.path(), legacy / entry.hash / file.path().filename());
    }
    const auto old =
        "[{\"hash\":\"" + two.hash + "\",\"enabled\":false},{\"hash\":\"" + one.hash + "\",\"enabled\":true}]";
    Write(legacy / "catalog.json", old);
    {
        StudioV3dmm::Store migrated(legacy);
        Check(Read(legacy / "catalog.json") == old);
        Check(migrated.Entries().size() == 2 && migrated.Entries()[0].hash == two.hash &&
              !migrated.Entries()[0].enabled);
        Check(migrated.Entries()[1].hash == one.hash && migrated.Entries()[1].shortName == "fixture");
        Check(migrated.Resolve(one.hash).members.size() == 2);
    }
    // SQLite is authoritative after migration, including across connections.
    Write(legacy / "catalog.json", "not JSON");
    StudioV3dmm::Store a(legacy), b(legacy);
    a.SetEnabled(one.hash, false);
    b.SetEnabled(two.hash, true);
    StudioV3dmm::Store current(legacy);
    Check(current.Entries()[0].enabled && !current.Entries()[1].enabled);
    // Missing/corrupt unrelated packages do not block metadata or a used package.
    Write(legacy / two.hash / "package.vxp", "broken");
    StudioV3dmm::Store unrelated(legacy);
    Check(unrelated.Entries().size() == 2 && unrelated.Resolve(one.hash).name == "One");
    Reject([&] { unrelated.Resolve(two.hash); });
    // A changed used file invalidates the persisted verification fingerprint.
    const auto member = unrelated.Resolve(one.hash).members.begin()->second;
    const auto original = Read(member);
    const auto stamp = fs::last_write_time(member);
    auto changed = original;
    changed.back() ^= 1;
    Write(member, changed);
    fs::last_write_time(member, stamp + std::chrono::seconds(2));
    Reject([&] { unrelated.Resolve(one.hash); });
    Write(member, original);
    unrelated.Resolve(one.hash);
    // Explicit verification also detects edits with preserved size/timestamp.
    const auto verifiedStamp = fs::last_write_time(member);
    Write(member, changed);
    fs::last_write_time(member, verifiedStamp);
    Reject([&] { unrelated.Resolve(one.hash, true); });
    Write(member, original);
    unrelated.Resolve(one.hash, true);
    // Reject ambiguous installed names and leave the catalog unchanged.
    Zip(root / "conflict.vxp", root / "content.3cn", "extra.3cn", {}, "One");
    Reject([&] { store.Install(V::WholeFile(root / "conflict.vxp")); });
    Check(StudioV3dmm::Store(store.Root()).Entries().size() == 2);
    // A failed migration rolls back; correcting its input permits a clean retry.
    const auto retry = root / "retry";
    fs::create_directory(retry);
    fs::create_directory(retry / one.hash);
    for (const auto &file : fs::directory_iterator(one.archive.parent_path()))
        fs::copy_file(file.path(), retry / one.hash / file.path().filename());
    const auto row = "{\"hash\":\"" + one.hash + "\",\"enabled\":true}";
    Write(retry / "catalog.json", "[" + row + "," + row + "]");
    Reject([&] { StudioV3dmm::Store bad(retry); });
    Write(retry / "catalog.json", "[" + row + "]");
    Check(StudioV3dmm::Store(retry).Resolve(one.hash).name == "One");
    const auto repair = root / "repair";
    fs::create_directory(repair);
    fs::create_directory(repair / one.hash);
    for (const auto &file : fs::directory_iterator(one.archive.parent_path()))
        if (file.path().filename() != "content.json")
            fs::copy_file(file.path(), repair / one.hash / file.path().filename());
    Write(repair / "catalog.json", "[" + row + "]");
    StudioV3dmm::Store incomplete(repair);
    Check(incomplete.Entries().front().name == "One" && !incomplete.Entries().front().error.empty());
    Reject([&] { incomplete.Resolve(one.hash); });
    fs::copy_file(one.archive.parent_path() / "content.json", repair / one.hash / "content.json");
    Check(incomplete.Resolve(one.hash).members.size() == 2 && incomplete.Entries().front().error.empty());
}
void Tests(const fs::path &root)
{
    DuplicateTests(root);
    Fixture(root / "fixture.3mm");
    Zip(root / "fixture.vxp", root / "fixture.3mm");
    auto package = V::ReadPackage(V::WholeFile(root / "fixture.vxp"));
    Check(package.name == "Synthetic VXP" && package.metadata.at("unknown") == "retained");
    auto archive = V::WholeFile(root / "fixture.vxp");
    V::WriteBundle(V::WholeFile(root / "fixture.3mm"), {{"3dmm", {}}, {package.name, archive}, {"Doraemon", {}}},
                   root / "fixture.vmm");
    auto bundle = V::ReadBundle(root / "fixture.vmm");
    Check(bundle.dependencies.size() == 3 && bundle.dependencies[2].archive.length == 0);
    Check(V::ReadPackage(bundle.dependencies[1].archive).name == package.name);
    V::Copy(bundle.movie, root / "inner.3mm");
    Check(Read(root / "inner.3mm") == Read(root / "fixture.3mm"));
    fs::create_directory(root / "extract");
    auto files = V::Materialize(bundle.dependencies[1].archive, root / "extract");
    Check(files.size() == 2 && Read(files.at("fixture.3cn")) == Read(root / "fixture.3mm"));
    auto original = Read(root / "fixture.vmm");
    Write(root / "bad.vmm", original.substr(0, original.size() - bundle.movie.length));
    Reject([&] { V::ReadBundle(root / "bad.vmm"); });
    auto corrupt = original;
    corrupt[std::size_t(bundle.dependencies[1].archive.offset) - 2] = 0;
    Write(root / "bad.vmm", corrupt);
    Reject([&] { V::ReadBundle(root / "bad.vmm"); });
    V::Limits small;
    small.memberBytes = 1;
    Reject([&] { V::ReadPackage(archive, small); });
    Zip(root / "unsafe.vxp", root / "fixture.3mm", "../escaped.3cn");
    Reject([&] { V::ReadPackage(V::WholeFile(root / "unsafe.vxp")); });
    Check(!fs::exists(root / "escaped.3cn"));
    Zip(root / "duplicate.vxp", root / "fixture.3mm", "FIXTURE.3CN");
    Reject([&] { V::ReadPackage(V::WholeFile(root / "duplicate.vxp")); });
    Zip(root / "identical.vxp", root / "fixture.3mm", "fixture.3cn");
    Check(V::ReadPackage(V::WholeFile(root / "identical.vxp")).members.size() == 3);
    StudioV3dmm::Store duplicateStore(root / "duplicate-store");
    duplicateStore.Install(V::WholeFile(root / "identical.vxp"));
    StudioV3dmm::Store duplicateReopened(root / "duplicate-store");
    Check(duplicateReopened.Entries().size() == 1 && duplicateReopened.Entries().front().members.size() == 2);
    Zip(root / "override.vxp", root / "fixture.3mm", {}, "RegID=27\r\n");
    Reject([&] { V::ReadPackage(V::WholeFile(root / "override.vxp")); });
    Zip(root / "legacy.vxp", root / "fixture.3mm", "caf\xe9.3cn");
    auto legacy = V::ReadPackage(V::WholeFile(root / "legacy.vxp"));
    Check(legacy.members.back().name == "caf\xe9.3cn");
    // Corrupt the content CRC in the central directory, leaving CFG readable.
    auto damaged = Read(root / "fixture.vxp");
    auto central = damaged.find(std::string("PK\1\2", 4));
    central = damaged.find(std::string("PK\1\2", 4), central + 4);
    Check(central != std::string::npos && central + 16 < damaged.size());
    damaged[central + 16] ^= 1;
    Write(root / "crc.vxp", damaged);
    StudioV3dmm::Store crcStore(root / "crc-store");
    Reject([&] { crcStore.Install(V::WholeFile(root / "crc.vxp")); });
    Check(crcStore.Entries().empty());
    for (const auto &file : fs::directory_iterator(root / "crc-store"))
        Check(file.is_regular_file() && file.path().filename().u8string().find("catalog.sqlite3") == 0);
    V::WriteBundle(bundle.movie, bundle.dependencies, root / "roundtrip.vmm");
    Check(Read(root / "roundtrip.vmm") == original);
    StudioV3dmm::Store store(root / "store");
    auto entry = store.Install(bundle.dependencies[1].archive);
    Check(entry.name == package.name && store.Entries().size() == 1);
    Check(Read(entry.archive) == Read(root / "fixture.vxp"));
    store.Install(archive);
    Check(store.Entries().size() == 1);
    store.SetEnabled(entry.hash, false);
    StudioV3dmm::Store reopened(root / "store");
    Check(reopened.Entries().size() == 1 && !reopened.Entries().front().enabled);
    auto changed = Read(entry.members.begin()->second);
    changed.back() ^= 1;
    Write(entry.members.begin()->second, changed);
    StudioV3dmm::Store altered(root / "store");
    Check(altered.Entries().size() == 1); // Listing does not read unused package content.
    Reject([&] { altered.Resolve(entry.hash); });
    auto failed = root / "bad-publication";
    fs::create_directory(failed);
    Write(root / "candidate", "new data");
    Reject([&] { V::Publish(root / "candidate", failed); });
    Check(fs::is_directory(failed) && Read(root / "candidate") == "new data");
    Check(Read(root / "fixture.vmm") == original);
}
int main(int argc, char **argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--inspect")
        {
            auto bundle = V::ReadBundle(fs::u8path(argv[2]));
            unsigned embedded = 0;
            for (const auto &dependency : bundle.dependencies)
                if (dependency.archive.length)
                {
                    V::ReadPackage(dependency.archive);
                    ++embedded;
                }
            std::cout << "dependencies=" << bundle.dependencies.size() << " embedded=" << embedded
                      << " movie_offset=" << bundle.movie.offset << " movie_bytes=" << bundle.movie.length << '\n';
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--materialize")
        {
            fs::create_directories(fs::u8path(argv[3]));
            auto files = V::Materialize(V::WholeFile(fs::u8path(argv[2])), fs::u8path(argv[3]));
            std::cout << "Name\t" << V::ReadPackage(V::WholeFile(fs::u8path(argv[2]))).name << '\n';
            for (const auto &file : files)
                std::cout << file.first << '\t' << file.second.u8string() << '\n';
            return 0;
        }
        auto parent = fs::absolute(argc > 1 ? argv[1] : "tmp/v3dmm-unit");
        fs::create_directories(parent);
        auto root = parent / ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(root);
        Tests(root);
        CatalogTests(root / "catalog-checks");
        std::cout << "V3DMM format/store tests passed: " << root << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
