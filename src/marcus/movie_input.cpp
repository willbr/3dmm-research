#include "movie_input.h"
#include "archive_file.h"
#include "movie_zip.h"
#include "mm/content_vfs.hpp"
#include "font_catalog.h"
#include <SDL3/SDL.h>
#include <exception>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;
namespace
{
void Require(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}
std::uint64_t ZipSize(MMZipSize size)
{
    return (std::uint64_t(size.high) << 32) | size.low;
}
void RequireZip(const char *error)
{
    if (error)
        throw std::runtime_error(error);
}
struct Cancellation
{
    const std::function<bool()> &check;
    std::exception_ptr error;
    static int Check(void *context)
    {
        auto &operation = *static_cast<Cancellation *>(context);
        try { return operation.check && operation.check(); }
        catch (...) { operation.error = std::current_exception(); return 1; }
    }
};
struct WorkingFiles
{
    MarcusMovieFile &file;
    const fs::path &source;
    std::exception_ptr error;

    static MMMovieStageResult Reserve(void *context, const char *directory, const char *leaf) noexcept
    {
        auto &files = *static_cast<WorkingFiles *>(context);
        try
        {
            auto path = fs::temp_directory_path() / directory;
            auto candidate = path / leaf;
            if (!fs::create_directory(path)) return MM_MOVIE_STAGE_COLLISION;
            files.file.directory = std::move(path);
            files.file.path = std::move(candidate);
            return MM_MOVIE_STAGE_OK;
        }
        catch (...) { files.error = std::current_exception(); return MM_MOVIE_STAGE_ERROR; }
    }

    static int CopyWritable(void *context) noexcept
    {
        auto &files = *static_cast<WorkingFiles *>(context);
        try
        {
            fs::copy_file(files.source, files.file.path);
            fs::permissions(files.file.path, fs::perms::owner_write, fs::perm_options::add);
            return 1;
        }
        catch (...) { files.error = std::current_exception(); return 0; }
    }
};
struct Sink
{
    std::ofstream stream;
    MMZipOutput output{};
    const std::function<bool()> &cancelled;
    std::exception_ptr error;
    Sink(const fs::path &path, MMU32 expected, const std::function<bool()> &cancelled)
        : stream(path, std::ios::binary), cancelled(cancelled)
    {
        Require(bool(stream), "Cannot create the private movie file.");
        mm_zip_output_init(&output, expected, Append, this);
    }
    static int Append(void *context, const void *bytes, size_t count)
    {
        auto &sink = *static_cast<Sink *>(context);
        try
        {
            if (sink.cancelled && sink.cancelled())
                return 0;
            return bool(sink.stream.write(static_cast<const char *>(bytes), static_cast<std::streamsize>(count)));
        }
        catch (...) { sink.error = std::current_exception(); return 0; }
    }
    bool Complete(MMU32 expectedCrc)
    {
        stream.close();
        return bool(stream) && mm_zip_output_complete(&output, expectedCrc);
    }
};
// Legacy ZIP methods (notably Shrink in aacw01.zip) are delegated only to an
// installed 7-Zip. No shell, installation, archive-wide extraction or filenames
// from the ZIP are used as filesystem targets. The sink still enforces size/CRC.
void LegacyExtract(const fs::path &archive, const MarcusZipEntry &member, Sink &sink)
{
    const auto path = archive.u8string();
    SDL_Process *process = nullptr;
    const char *programs[] = {"7z", "7zz", "C:/Program Files/7-Zip/7z.exe", "C:/Program Files (x86)/7-Zip/7z.exe"};
    for (const auto *program : programs)
    {
        const char *args[] = {program, "x", "-so", "-spd", "-p-", "--", path.c_str(), member.name, nullptr};
        const auto properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char **>(args));
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
        process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (process)
            break;
    }
    Require(process != nullptr, "This ZIP uses legacy compression. Install 7-Zip to open this movie.");
    const auto destroy = [](SDL_Process *value) {
        int status;
        if (!SDL_WaitProcess(value, false, &status))
        {
            SDL_KillProcess(value, true);
            SDL_WaitProcess(value, true, &status);
        }
        SDL_DestroyProcess(value);
    };
    std::unique_ptr<SDL_Process, decltype(destroy)> owner(process, destroy);
    auto output = SDL_GetProcessOutput(process);
    Require(output != nullptr, "Cannot read 7-Zip's movie output.");
    std::array<unsigned char, 65536> bytes;
    bool good = true;
    const auto deadline = SDL_GetTicks() + 60000;
    for (;;)
    {
        if ((sink.cancelled && sink.cancelled()) || SDL_GetTicks() >= deadline)
        {
            good = false;
            SDL_KillProcess(process, true);
            break;
        }
        const auto count = SDL_ReadIO(output, bytes.data(), bytes.size());
        if (!count)
        {
            if (SDL_GetIOStatus(output) == SDL_IO_STATUS_NOT_READY)
            {
                SDL_Delay(5);
                continue;
            }
            break;
        }
        if (!mm_zip_output_write(&sink.output, {sink.output.written, 0}, bytes.data(), count))
        {
            good = false;
            SDL_KillProcess(process, true);
            break;
        }
    }
    int status = -1;
    const bool exited = SDL_WaitProcess(process, true, &status);
    if (sink.error) std::rethrow_exception(sink.error);
    Require(exited && status == 0 && good,
            "7-Zip could not extract this movie, or its size exceeded the ZIP declaration.");
}
void Extract(MMZip *zip, const fs::path &archive, const MarcusZipList &list, const MarcusZipEntry &entry,
             const fs::path &target, const std::function<bool()> &cancelled, bool font)
{
    Sink sink(target, entry.member.bytes.low, cancelled);
    if (entry.member.supported)
    {
        const auto extracted = mm_zip_extract(zip, entry.slot, mm_zip_output_write, &sink.output);
        if (sink.error) std::rethrow_exception(sink.error);
        Require(extracted != 0, font ? std::string("Cannot extract bundled font (data or CRC error): ") + entry.name
                                     : "Cannot extract the selected ZIP movie (data or CRC error).");
    }
    else
    {
        Require(marcus_zip_legacy_unambiguous(&list, &entry),
            font ? "Legacy-compressed duplicate font names cannot be selected unambiguously."
                 : "Legacy-compressed duplicate movie names cannot be selected unambiguously.");
        LegacyExtract(archive, entry, sink);
    }
    Require(sink.Complete(entry.member.crc), font ? std::string("Bundled font does not match its ZIP size or CRC: ") + entry.name
                                                  : "The extracted movie does not match its ZIP size or CRC.");
}
} // namespace

struct MarcusMovieInput::Impl
{
    fs::path path;
    std::ifstream stream;
    MMZip *zip = nullptr;
    MarcusZipCatalog catalog{};
    std::uint64_t size = 0;
    std::vector<std::weak_ptr<MarcusMovieFile>> extracted;
    ~Impl()
    {
        marcus_zip_catalog_clear(&catalog);
        mm_zip_close(zip);
    }
};
MarcusMovieFile::~MarcusMovieFile()
{
    if (!directory.empty())
    {
        std::error_code ignored;
        fonts.reset();
        for (const auto &font : ownedFonts)
            fs::remove(font, ignored);
        if (!fontDirectory.empty() && fontDirectory.parent_path() == directory) fs::remove(fontDirectory, ignored);
        fs::remove(path, ignored);
        fs::remove(directory, ignored);
    }
}
std::shared_ptr<MarcusMovieFile> MarcusCreateWorkingFile(int kind, const fs::path &source)
{
    auto file = std::make_shared<MarcusMovieFile>(fs::path{});
    const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    WorkingFiles state{*file, source, {}};
    const MarcusWorkingFiles services{&state, WorkingFiles::Reserve, WorkingFiles::CopyWritable};
    const bool staged = marcus_archive_stage_working_file(kind, stamp.c_str(), !source.empty(), &services);
    if (state.error) std::rethrow_exception(state.error);
    if (!staged)
        throw std::runtime_error(kind == MARCUS_WORKING_BOARD ? "Cannot create storyboard working directory."
                                                            : "Cannot reserve a new movie working file.");
    return file;
}

MarcusMovieInput::MarcusMovieInput() : impl(new Impl)
{
}
MarcusMovieInput::~MarcusMovieInput() = default;
void MarcusMovieFile::LoadFonts(const fs::path &directory)
{
    auto files = std::make_shared<MM::Content::Vfs>();
    std::vector<MM::Content::File> mounts;
    for (const auto &entry : fs::directory_iterator(directory))
        if (!entry.is_symlink() && entry.is_regular_file() && marcus_zip_is_font(entry.path().u8string().c_str()))
            mounts.push_back({entry.path().filename().u8string(), entry.path()});
    files->Mount("movie-fonts", mounts);
    auto catalog = std::make_shared<KauaiFonts::FontCatalog>();
    for (const auto &entry : files->Entries("movie-fonts"))
    {
        if (!marcus_zip_is_font(entry.name.c_str())) continue;
        auto stream = files->Open("movie-fonts", entry.name);
        stream.seekg(0, std::ios::end);
        const auto size = stream.tellg();
        Require(size > 0 && std::uint64_t(size) <= MARCUS_ZIP_FONT_FILE_BYTES, "Invalid bundled font size: " + entry.name);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(bool(stream) && catalog->AddMemory(std::move(bytes)), "Cannot decode bundled font: " + entry.name);
    }
    fontDirectory = directory;
    fontFiles = std::move(files);
    fonts = std::move(catalog);
}
void MarcusMovieInput::Open(const std::string &path, const std::function<bool()> &cancelled)
{
    auto opened = std::make_unique<Impl>();
    opened->path = fs::absolute(fs::u8path(path));
    opened->stream.open(opened->path, std::ios::binary | std::ios::ate);
    Require(bool(opened->stream), "Cannot open the movie or ZIP file.");
    const auto size = opened->stream.tellg();
    Require(size >= 0, "Cannot determine the movie file size.");
    opened->size = static_cast<std::uint64_t>(size);
    opened->stream.seekg(0);
    char signature[4] = {};
    opened->stream.read(signature, sizeof(signature));
    opened->stream.clear();
    if (marcus_zip_signature(path.c_str(), signature))
    {
        const MMZipInput source{&opened->stream, {static_cast<MMU32>(opened->size), static_cast<MMU32>(opened->size >> 32)},
            [](void *context, MMZipSize offset, void *bytes, size_t count) -> int {
                try
                {
                    auto &input = *static_cast<std::ifstream *>(context);
                    input.clear();
                    input.seekg(static_cast<std::streamoff>(ZipSize(offset)));
                    return bool(input.read(static_cast<char *>(bytes), static_cast<std::streamsize>(count)));
                }
                catch (...) { return 0; }
            }};
        Require(mm_zip_open(&source, &opened->zip) == MM_ZIP_OK, "Invalid ZIP archive.");
        Cancellation cancellation{cancelled, {}};
        const auto error = marcus_zip_catalog_read(opened->zip, Cancellation::Check, &cancellation, &opened->catalog);
        if (cancellation.error) std::rethrow_exception(cancellation.error);
        RequireZip(error);
    }
    opened->extracted.resize(opened->zip ? opened->catalog.movies.count : 1);
    impl = std::move(opened);
    prepared.reset();
}
int MarcusMovieInput::Count() const
{
    return static_cast<int>(impl->extracted.size());
}
MarcusMovieEntry MarcusMovieInput::Entry(int index) const
{
    Require(index >= 0 && index < Count(), "Movie entry is outside the ZIP list.");
    MarcusMovieEntry result = {};
    if (impl->zip)
    {
        const auto &entry = impl->catalog.movies.items[index];
        std::memcpy(result.name, entry.name, std::strlen(entry.name) + 1);
        result.bytes = static_cast<double>(ZipSize(entry.member.bytes));
        result.zipped = 1;
    }
    else
    {
        const auto name = impl->path.filename().u8string();
        std::memcpy(result.name, name.data(), std::min(name.size(), sizeof(result.name) - 1));
        result.bytes = static_cast<double>(impl->size);
    }
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusMovieInput::Materialize(int index, const std::function<bool()> &cancelled)
{
    Require(!cancelled || !cancelled(), "Movie preparation cancelled.");
    Require(index >= 0 && index < Count(), "Select a movie from this ZIP before opening it.");
    auto &cached = impl->extracted[index];
    if (auto existing = cached.lock())
        return existing;
    auto selected = std::make_shared<MarcusMovieFile>(impl->zip ? fs::path{} : impl->path);
    if (impl->zip)
    {
        const auto &movies = impl->catalog.movies;
        const auto &movie = movies.items[index];
        RequireZip(marcus_zip_check_movie(&movie));
        for (int attempt = 0; attempt < 16 && selected->directory.empty(); ++attempt)
        {
            const auto candidate =
                fs::temp_directory_path() / ("marcus-movie-" + std::to_string(std::random_device{}()));
            if (fs::create_directory(candidate))
                selected->directory = candidate;
        }
        Require(!selected->directory.empty(), "Cannot create a private ZIP movie directory.");
        selected->path = selected->directory / (std::string("movie") + movie.extension);
        Extract(impl->zip, impl->path, movies, movie, selected->path, cancelled, false);
        const auto &fonts = impl->catalog.fonts;
        Require(marcus_zip_check_fonts(&fonts),
            "Bundled fonts must be unencrypted, at most 16 MiB each and 64 MiB in total.");
        if (fonts.count)
        {
            selected->fontDirectory = selected->directory / "fonts";
            Require(fs::create_directory(selected->fontDirectory), "Cannot create the private font cache.");
            for (MMU32 i = 0; i < fonts.count; ++i)
            {
                const auto &font = fonts.items[i];
                const auto target = selected->fontDirectory / ("font-" + std::to_string(font.slot) + font.extension);
                selected->ownedFonts.push_back(target);
                Extract(impl->zip, impl->path, fonts, font, target, cancelled, true);
            }
            selected->LoadFonts(selected->fontDirectory);
        }
    }
    cached = selected;
    return selected;
}
extern "C"
{
    MarcusMovieInput *marcus_input_create(void)
    {
        try
        {
            return new MarcusMovieInput;
        }
        catch (...)
        {
            return nullptr;
        }
    }
    void marcus_input_destroy(MarcusMovieInput *input)
    {
        delete input;
    }
    const char *marcus_input_error(const MarcusMovieInput *input)
    {
        return input ? input->error : "Movie input is unavailable.";
    }
    int marcus_input_open(MarcusMovieInput *input, const char *path)
    {
        if (!input)
            return 0;
        input->error[0] = 0;
        try
        {
            Require(path && *path, "Choose a movie or ZIP file.");
            input->Open(path);
            return 1;
        }
        catch (const std::exception &error)
        {
            std::snprintf(input->error, sizeof(input->error), "%s", error.what());
        }
        catch (...)
        {
            std::snprintf(input->error, sizeof(input->error), "Unexpected error reading movie input.");
        }
        return 0;
    }
    int marcus_input_count(const MarcusMovieInput *input)
    {
        return input ? input->Count() : 0;
    }
    int marcus_input_entry(const MarcusMovieInput *input, int index, MarcusMovieEntry *entry)
    {
        if (!input || !entry || index < 0 || index >= input->Count())
            return 0;
        try
        {
            *entry = input->Entry(index);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }
}
