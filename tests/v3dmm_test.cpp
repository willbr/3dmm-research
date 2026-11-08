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
void Zip(const fs::path &path, const fs::path &content, const std::string &extra = {}, const std::string &settings = {})
{
    auto data = Read(content);
    mz_zip_archive zip{};
    Check(mz_zip_writer_init_heap(&zip, 0, 0) != 0);
    std::string cfg = "Name=Synthetic VXP\r\nAuthor=3DMMForever\r\nType=Portable\r\nUnknown=retained\r\n";
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
void Tests(const fs::path &root)
{
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
    Check(crcStore.Entries().empty() && fs::is_empty(root / "crc-store"));
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
    Reject([&] { StudioV3dmm::Store altered(root / "store"); });
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
        auto parent = fs::absolute(argc > 1 ? argv[1] : "tmp/v3dmm-unit");
        fs::create_directories(parent);
        auto root = parent / ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(root);
        Tests(root);
        std::cout << "V3DMM format/store tests passed: " << root << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
