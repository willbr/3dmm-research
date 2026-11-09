#include "content.h"
#include "studio_vxp_store.h"
#include "studio_settings.h"
#include <SDL3/SDL.h>
#include <miniz.h>
#include <chrono>
#include <cstdlib>
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
        throw std::runtime_error("Marcus content test failed");
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
void Environment(const char *name, const std::string &value)
{
#ifdef _WIN32
    Check(_putenv_s(name, value.c_str()) == 0);
#else
    Check(setenv(name, value.c_str(), 1) == 0);
#endif
    Check(std::getenv(name) && value == std::getenv(name));
}
void Fixture(const fs::path &path, byte value)
{
    auto absolute = path.string();
    String text;
    text.SetSzs(absolute.data());
    Filename filename;
    Check(filename.FBuildFromPath(&text));
    auto file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
    Check(file && file->FPutPv(&value, 1, 'TEST', value) && file->FSave(kctgSoc));
    ReleasePpo(&file);
}
void Package(const fs::path &path, const std::string &shortName, const fs::path &content,
             const std::string &displayName = "Stage")
{
    mz_zip_archive zip{};
    Check(mz_zip_writer_init_heap(&zip, 0, 0) != 0);
    const auto cfg = "Name=" + displayName + "\nAuthor=3DMMForever\nType=Portable\n";
    Check(mz_zip_writer_add_mem(&zip, (shortName + ".cfg").c_str(), cfg.data(), cfg.size(), 0) != 0);
    const auto data = Read(content);
    for (auto ext : {".3cn", ".3th"})
        Check(mz_zip_writer_add_mem(&zip, (shortName + ext).c_str(), data.data(), data.size(), 0) != 0);
    Check(mz_zip_writer_add_mem(&zip, "shared.3cn", data.data(), data.size(), 0) != 0);
    void *bytes = nullptr;
    std::size_t length = 0;
    Check(mz_zip_writer_finalize_heap_archive(&zip, &bytes, &length) != 0);
    Write(path, std::string(static_cast<char *>(bytes), length));
    mz_free(bytes);
    mz_zip_writer_end(&zip);
}
void Open(const fs::path &movie, const fs::path &first, const fs::path &second, const std::string &shared,
          const fs::path &installed = {})
{
    const auto before = Read(movie);
    fs::path work;
    {
        MarcusContent content(movie.u8string(), {});
        Check(content.bundled);
        work = content.movie.parent_path();
        Check(Read(content.movie) == "CHN2fixture");
        Check(content.files.Files("v3dmm", ".3cn").size() == 3);
        Check(Read(content.files.Resolve("v3dmm", "pkabj_stage.3cn")) == Read(first));
        Check(Read(content.files.Resolve("v3dmm", "Stage.3cn")) == Read(second));
        Check(Read(content.files.Resolve("v3dmm", "shared.3cn")) == shared);
        if (!installed.empty())
            Check(content.files.Resolve("v3dmm", "pkabj_stage.3cn") == installed);
    }
    Check(!fs::exists(work) && Read(movie) == before);
}
void Tests(const fs::path &root)
{
    Environment("KAUAI_TEST_SETTINGS_RUN", "marcus-content-" + std::to_string(SDL_GetPerformanceCounter()));
    const auto settings = fs::u8path(StudioSettings::IniPath());
    const bool settingsExisted = fs::exists(settings);
    const auto settingsBefore = Read(settings);
    Write(root / "movie.3mm", "CHN2fixture");
    Fixture(root / "first.3cn", 1);
    Fixture(root / "second.3cn", 2);
    Package(root / "first.vxp", "pkabj_stage", root / "first.3cn");
    Package(root / "second.vxp", "Stage", root / "second.3cn", "sTaGe");
    const auto first = V::WholeFile(root / "first.vxp"), second = V::WholeFile(root / "second.vxp");
    const auto firstBytes = Read(first.file), secondBytes = Read(second.file);
    V::WriteBundle(V::WholeFile(root / "movie.3mm"), {{"Stage", first}, {"sTaGe", second}}, root / "movie.vmm");
    Environment("THREEDMM_PACK_DIR", (root / "packs").u8string());
    Open(root / "movie.vmm", root / "first.3cn", root / "second.3cn", Read(root / "first.3cn"));
    Check(!fs::exists(root / "packs/v3dmm/catalog.json"));
    // An installed same-name package may satisfy its own shortname, never
    // swallow the other embedded package. No new imports/catalog edits occur.
    StudioV3dmm::Store store(root / "packs/v3dmm");
    const auto installed = store.Install(first);
    const auto catalog = Read(root / "packs/v3dmm/catalog.json");
    const auto archived = Read(installed.archive);
    Open(root / "movie.vmm", root / "first.3cn", root / "second.3cn", Read(root / "first.3cn"),
         installed.members.at("pkabj_stage.3cn"));
    V::WriteBundle(V::WholeFile(root / "movie.3mm"), {{"sTaGe", second}, {"Stage", first}}, root / "reverse.vmm");
    Open(root / "reverse.vmm", root / "first.3cn", root / "second.3cn", Read(root / "second.3cn"));
    Check(Read(root / "packs/v3dmm/catalog.json") == catalog && Read(installed.archive) == archived);
    Check(fs::exists(installed.members.at("pkabj_stage.3cn")));
    Check(Read(root / "first.vxp") == Read(installed.archive));
    // A repeated declaration must still validate its embedded CFG before it
    // can reuse installed content with the same shortname.
    Package(root / "mismatch.vxp", "pkabj_stage", root / "second.3cn", "Wrong name");
    V::WriteBundle(V::WholeFile(root / "movie.3mm"), {{"Stage", first}, {"Stage", V::WholeFile(root / "mismatch.vxp")}},
                   root / "mismatch.vmm");
    bool rejected = false;
    try
    {
        MarcusContent invalid((root / "mismatch.vmm").u8string(), {});
    }
    catch (const std::exception &error)
    {
        rejected = std::string(error.what()) == "Embedded VXP name does not match its VMM dependency";
    }
    Check(rejected && Read(root / "packs/v3dmm/catalog.json") == catalog);
    Check(Read(first.file) == firstBytes && Read(second.file) == secondBytes);
    Check(fs::exists(settings) == settingsExisted && Read(settings) == settingsBefore);
}
int main(int argc, char **argv)
{
    try
    {
        const auto parent = fs::absolute(argc > 1 ? argv[1] : "tmp/marcus-content");
        const auto root =
            parent / ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        Tests(root);
        std::cout << "Marcus content: repeated names, installed shortnames, mount order, cleanup and read-only inputs "
                     "passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
