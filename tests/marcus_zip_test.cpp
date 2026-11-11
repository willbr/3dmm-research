#include "movie_input.h"
#include "content.h"
#include "three_mm/v3dmm.h"
#include <miniz.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;
namespace V = ThreeMM::V3dmm;
void Check(bool value, int line)
{
    if (!value)
        throw std::runtime_error("ZIP check failed at line " + std::to_string(line));
}
#define CHECK(value) Check((value), __LINE__)
std::string Read(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    CHECK(bool(input) && input.tellg() >= 0);
    std::string bytes(static_cast<std::size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!bytes.empty())
        input.read(&bytes[0], static_cast<std::streamsize>(bytes.size()));
    CHECK(bool(input));
    return bytes;
}
void Write(const fs::path &path, const std::string &bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), bytes.size());
    CHECK(bool(output));
}
void Zip(const fs::path &path, const std::vector<std::pair<std::string, std::string>> &members, int level = 6)
{
    mz_zip_archive zip = {};
    CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
    for (const auto &member : members)
        CHECK(mz_zip_writer_add_mem(&zip, member.first.c_str(), member.second.data(), member.second.size(), level));
    void *bytes = nullptr;
    std::size_t length = 0;
    CHECK(mz_zip_writer_finalize_heap_archive(&zip, &bytes, &length));
    Write(path, std::string(static_cast<char *>(bytes), length));
    mz_free(bytes);
    mz_zip_writer_end(&zip);
}
std::set<fs::path> TemporaryMovies()
{
    std::set<fs::path> result;
    for (const auto &entry : fs::directory_iterator(fs::temp_directory_path()))
        if (entry.path().filename().u8string().find("marcus-movie-") == 0)
            result.insert(entry.path());
    return result;
}
void Rejected(const fs::path &path)
{
    const auto before = Read(path);
    const auto directories = TemporaryMovies();
    bool rejected = false;
    try
    {
        MarcusMovieInput input;
        input.Open(path.u8string());
        input.Materialize(0);
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    CHECK(rejected && Read(path) == before && TemporaryMovies() == directories);
}
void Environment(const char *name, const std::string &value)
{
#ifdef _WIN32
    CHECK(_putenv_s(name, value.c_str()) == 0);
#else
    CHECK(setenv(name, value.c_str(), 1) == 0);
#endif
}
void Tests(const fs::path &root)
{
    const auto originalDirectories = TemporaryMovies();
    Zip(root / "one.zip",
        {{"readme.txt", "leave this alone"}, {"sub folder/Movie.3MM", "CHN2one"}, {"pack.vxp", "not extracted"}});
    const auto original = Read(root / "one.zip");
    fs::path directory;
    std::shared_ptr<MarcusMovieFile> held;
    {
        MarcusMovieInput input;
        input.Open((root / "one.zip").u8string());
        CHECK(input.Count() == 1 && std::string(input.Entry(0).name) == "sub folder/Movie.3MM");
        CHECK(TemporaryMovies() == originalDirectories); // Listing extracts nothing.
        held = input.Materialize(0);
        directory = held->directory;
        CHECK(Read(held->path) == "CHN2one" && held == input.Materialize(0));
        CHECK(std::distance(fs::directory_iterator(directory), fs::directory_iterator()) == 1);
    }
    CHECK(fs::exists(held->path)); // Input/list lifetime is independent of the selected movie lease.
    held.reset();
    CHECK(!fs::exists(directory) && Read(root / "one.zip") == original);
    // Background preparation can be cancelled partway through decompression.
    // Partial files disappear, the source stays intact, and retry still works.
    Zip(root / "cancel.zip", {{"large.3mm", std::string(2 * 1024 * 1024, 'x')}});
    {
        MarcusMovieInput input;
        input.Open((root / "cancel.zip").u8string());
        const auto before = TemporaryMovies();
        int polls = 0;
        bool cancelled = false;
        try { input.Materialize(0, [&] { return ++polls > 2; }); }
        catch (const std::exception &) { cancelled = true; }
        CHECK(cancelled && polls > 2 && TemporaryMovies() == before);
        input.prepared = input.Materialize(0);
        const auto extracted = input.prepared->path;
        CHECK(Read(extracted).size() == 2 * 1024 * 1024);
        CHECK(input.Materialize(0, [] { return false; })->path == extracted);
        // Opening a different input releases the pinned preparation.
        input.Open((root / "one.zip").u8string());
        CHECK(!fs::exists(extracted));
        bool listingCancelled = false;
        try { input.Open((root / "cancel.zip").u8string(), [] { return true; }); }
        catch (const std::exception &) { listingCancelled = true; }
        CHECK(listingCancelled && input.Count() == 1 && std::string(input.Entry(0).name) == "sub folder/Movie.3MM");
    }
    Zip(root / "multi.zip", {{"nested/one.3mm", "first"}, {"nested/two.VMM", "second"}, {"nested/one.3mm", "third"}});
    {
        MarcusMovieInput input;
        input.Open((root / "multi.zip").u8string());
        CHECK(input.Count() == 3);
        for (int i = 0; i < 3; ++i)
        {
            const auto file = input.Materialize(i);
            CHECK(Read(file->path) == (i == 0 ? "first" : i == 1 ? "second" : "third"));
        }
    }
    MarcusMovieInput *list = marcus_input_create();
    CHECK(list && marcus_input_open(list, (root / "multi.zip").u8string().c_str()));
    MarcusMovieEntry entry;
    CHECK(marcus_input_count(list) == 3 && marcus_input_entry(list, 1, &entry));
    CHECK(std::string(entry.name) == "nested/two.VMM" && entry.zipped && entry.bytes == 6);
    CHECK(!marcus_input_entry(list, -1, &entry) && !marcus_input_entry(list, 3, &entry));
    CHECK(!marcus_input_open(list, (root / "absent.zip").u8string().c_str()));
    CHECK(marcus_input_count(list) == 3); // Failed listing retains the previous valid list.
    marcus_input_destroy(list);
    Write(root / "plain.3mm", "CHN2raw");
    {
        MarcusMovieInput input;
        input.Open((root / "plain.3mm").u8string());
        auto file = input.Materialize(0);
        CHECK(input.Count() == 1 && !input.Entry(0).zipped && file->directory.empty());
        CHECK(file->path == root / "plain.3mm");
    }
    CHECK(Read(root / "plain.3mm") == "CHN2raw");
    // A VMM inside a ZIP retains the existing bundle loader and its separate
    // inner-movie staging. Both leases are cleaned after content destruction.
    Zip(root / "pack.vxp", {{"zip.cfg", "Name=ZIP fixture\nAuthor=3DMMForever\nType=Portable\n"},
                            {"zip.3cn", "CHN2content"},
                            {"zip.3th", "CHN2thumbnail"}});
    V::WriteBundle(V::WholeFile(root / "plain.3mm"), {{"ZIP fixture", V::WholeFile(root / "pack.vxp")}},
                   root / "bundle.vmm");
    const auto bundle = Read(root / "bundle.vmm");
    Zip(root / "bundle.zip", {{"nested/bundle.VMM", bundle}});
    Environment("KAUAI_TEST_SETTINGS_RUN", "marcus-zip-" + root.filename().u8string());
    Environment("THREEDMM_PACK_DIR", (root / "packs").u8string());
    fs::path inner;
    {
        auto input = std::make_unique<MarcusMovieInput>();
        input->Open((root / "bundle.zip").u8string());
        auto file = input->Materialize(0);
        directory = file->directory;
        MarcusContent content(file, {});
        file.reset();
        input.reset();
        CHECK(content.bundled && Read(content.movie) == "CHN2raw" && fs::exists(directory));
        inner = content.movie.parent_path();
    }
    CHECK(!fs::exists(directory) && !fs::exists(inner) && Read(root / "bundle.vmm") == bundle);
    Zip(root / "none.zip", {{"readme.txt", "no movies"}});
    Rejected(root / "none.zip");
    Write(root / "invalid.zip", "PK invalid");
    Rejected(root / "invalid.zip");
    Zip(root / "unsafe.zip", {{"../movie.3mm", "CHN2"}});
    Rejected(root / "unsafe.zip");
    Zip(root / "crc.zip", {{"movie.3mm", "CHN2CRC"}}, 0);
    auto corrupt = Read(root / "crc.zip");
    const auto data = corrupt.find("CHN2CRC");
    CHECK(data != std::string::npos);
    corrupt[data] ^= 1;
    Write(root / "crc.zip", corrupt);
    Rejected(root / "crc.zip");
    Zip(root / "oversized.zip", {{"movie.3mm", "CHN2"}}, 0);
    auto oversized = Read(root / "oversized.zip");
    const auto central = oversized.find(std::string("PK\1\2", 4));
    CHECK(central != std::string::npos);
    for (int byte = 0; byte < 4; ++byte)
    {
        oversized[central + 24 + byte] = static_cast<char>((512u * 1024 * 1024 + 1) >> (byte * 8));
        oversized[22 + byte] = oversized[central + 24 + byte];
    }
    Write(root / "oversized.zip", oversized);
    Rejected(root / "oversized.zip");
    CHECK(TemporaryMovies() == originalDirectories);
    std::cout << "ZIP input: nested and duplicate entries, explicit selection, CRC/size errors, temporary leases, "
                 "VMM loading and unchanged inputs passed.\n";
}
int main(int argc, char **argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--catalog")
        {
            int archives = 0, movies = 0, otherArchives = 0;
            const auto directories = TemporaryMovies();
            for (const auto &entry : fs::directory_iterator(fs::u8path(argv[2])))
            {
                if (V::Extension(entry.path().filename().u8string()) != ".zip")
                    continue;
                const auto before = Read(entry.path());
                MarcusMovieInput input;
                try
                {
                    input.Open(entry.path().u8string());
                }
                catch (const std::exception &error)
                {
                    if (std::string(error.what()) == "This ZIP contains no .3MM or .VMM movies.")
                    {
                        ++otherArchives;
                        continue;
                    }
                    throw std::runtime_error(entry.path().u8string() + ": " + error.what());
                }
                for (int index = 0; index < input.Count(); ++index)
                {
                    auto file = input.Materialize(index);
                    CHECK(fs::file_size(file->path) == static_cast<std::uint64_t>(input.Entry(index).bytes));
                    ++movies;
                }
                CHECK(Read(entry.path()) == before);
                ++archives;
            }
            CHECK(TemporaryMovies() == directories);
            std::cout << "ZIP catalog: " << archives << " archives, " << movies
                      << " selected movies extracted with size/CRC checks; " << otherArchives
                      << " ZIPs without native movies skipped; sources unchanged and temporary files removed.\n";
            return 0;
        }
        const auto root = fs::absolute(argc > 1 ? fs::u8path(argv[1]) : fs::path("tmp/marcus-zip")) /
                          ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        // The browser may be preparing previews concurrently in the system
        // temp folder. Check this test's leases in a private temp namespace.
        const auto temporary = root / "temp";
        fs::create_directories(temporary);
#ifdef _WIN32
        Environment("TMP", temporary.u8string());
        Environment("TEMP", temporary.u8string());
#else
        Environment("TMPDIR", temporary.u8string());
#endif
        Tests(root);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
