#include "movie_input.h"
#include <SDL3/SDL.h>
#include <miniz.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;
namespace
{
constexpr std::uint64_t movieBudget = 512ull * 1024 * 1024;
void Require(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}
std::string Extension(std::string name)
{
    const auto dot = name.find_last_of('.');
    name = dot == std::string::npos ? "" : name.substr(dot);
    for (auto &c : name)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return name;
}
bool SafeName(const std::string &name)
{
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos ||
        name.find('\0') != std::string::npos)
        return false;
    std::size_t begin = 0;
    while (begin < name.size())
    {
        auto end = name.find_first_of("/\\", begin);
        if (end == std::string::npos)
            end = name.size();
        if (name.substr(begin, end - begin) == "..")
            return false;
        begin = end + 1;
    }
    return true;
}
struct Member
{
    std::string name;
    unsigned index = 0, crc = 0, method = 0;
    std::uint64_t bytes = 0;
    bool encrypted = false, supported = true;
    std::weak_ptr<MarcusMovieFile> extracted;
};
struct Sink
{
    std::ofstream stream;
    std::uint64_t written = 0, expected;
    mz_ulong crc = MZ_CRC32_INIT;
    std::function<bool()> cancelled;
    Sink(const fs::path &path, std::uint64_t expected, std::function<bool()> cancelled)
        : stream(path, std::ios::binary), expected(expected), cancelled(std::move(cancelled))
    {
        Require(bool(stream), "Cannot create the private movie file.");
    }
    std::size_t Write(std::uint64_t offset, const void *data, std::size_t bytes)
    {
        if ((cancelled && cancelled()) || offset != written || bytes > expected - written)
            return 0;
        stream.write(static_cast<const char *>(data), static_cast<std::streamsize>(bytes));
        if (!stream)
            return 0;
        crc = mz_crc32(crc, static_cast<const unsigned char *>(data), bytes);
        written += bytes;
        return bytes;
    }
    bool Complete(unsigned expectedCrc)
    {
        stream.flush();
        return bool(stream) && written == expected && crc == expectedCrc;
    }
};
// Legacy ZIP methods (notably Shrink in aacw01.zip) are delegated only to an
// installed 7-Zip. No shell, installation, archive-wide extraction or filenames
// from the ZIP are used as filesystem targets. The sink still enforces size/CRC.
void LegacyExtract(const fs::path &archive, const Member &member, Sink &sink)
{
    const auto path = archive.u8string();
    SDL_Process *process = nullptr;
    const char *programs[] = {"7z", "7zz", "C:/Program Files/7-Zip/7z.exe", "C:/Program Files (x86)/7-Zip/7z.exe"};
    for (const auto *program : programs)
    {
        const char *args[] = {program, "x", "-so", "-spd", "-p-", "--", path.c_str(), member.name.c_str(), nullptr};
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
        if (sink.Write(sink.written, bytes.data(), count) != count)
        {
            good = false;
            SDL_KillProcess(process, true);
            break;
        }
    }
    int status = -1;
    Require(SDL_WaitProcess(process, true, &status) && status == 0 && good,
            "7-Zip could not extract this movie, or its size exceeded the ZIP declaration.");
}
} // namespace
struct MarcusMovieInput::Impl
{
    fs::path path;
    std::ifstream stream;
    mz_zip_archive zip = {};
    bool zipped = false;
    std::vector<Member> members;
    ~Impl()
    {
        mz_zip_reader_end(&zip);
    }
};
MarcusMovieFile::~MarcusMovieFile()
{
    if (!directory.empty())
    {
        std::error_code ignored;
        fs::remove(path, ignored);
        fs::remove(directory, ignored);
    }
}
MarcusMovieInput::MarcusMovieInput() : impl(new Impl)
{
}
MarcusMovieInput::~MarcusMovieInput() = default;
void MarcusMovieInput::Open(const std::string &path, const std::function<bool()> &cancelled)
{
    auto opened = std::make_unique<Impl>();
    opened->path = fs::absolute(fs::u8path(path));
    opened->stream.open(opened->path, std::ios::binary | std::ios::ate);
    Require(bool(opened->stream), "Cannot open the movie or ZIP file.");
    const auto size = opened->stream.tellg();
    Require(size >= 0, "Cannot determine the movie file size.");
    opened->stream.seekg(0);
    char signature[4] = {};
    opened->stream.read(signature, sizeof(signature));
    opened->stream.clear();
    opened->zipped = Extension(path) == ".zip" ||
                     (signature[0] == 'P' && signature[1] == 'K' &&
                      ((signature[2] == 3 && signature[3] == 4) || (signature[2] == 5 && signature[3] == 6)));
    if (!opened->zipped)
    {
        Member member;
        member.name = opened->path.filename().u8string();
        member.bytes = static_cast<std::uint64_t>(size);
        opened->members.push_back(std::move(member));
    }
    else
    {
        opened->zip.m_pIO_opaque = opened.get();
        opened->zip.m_pRead = [](void *context, mz_uint64 offset, void *data, std::size_t bytes) -> std::size_t {
            auto &stream = static_cast<Impl *>(context)->stream;
            stream.clear();
            stream.seekg(static_cast<std::streamoff>(offset));
            stream.read(static_cast<char *>(data), static_cast<std::streamsize>(bytes));
            return static_cast<std::size_t>(stream.gcount());
        };
        Require(mz_zip_reader_init(&opened->zip, static_cast<mz_uint64>(size), 0) != 0, "Invalid ZIP archive.");
        const auto count = mz_zip_reader_get_num_files(&opened->zip);
        Require(count <= 100000, "ZIP archives are limited to 100,000 members.");
        for (unsigned index = 0; index < count; ++index)
        {
            Require(!cancelled || !cancelled(), "Movie preparation cancelled.");
            mz_zip_archive_file_stat stat = {};
            Require(mz_zip_reader_file_stat(&opened->zip, index, &stat) != 0, "Cannot read a ZIP member record.");
            if (stat.m_is_directory)
                continue;
            const auto length = mz_zip_reader_get_filename(&opened->zip, index, nullptr, 0);
            Require(length > 1 && length <= 65536, "Invalid ZIP member name.");
            std::string name(length, '\0');
            Require(mz_zip_reader_get_filename(&opened->zip, index, &name[0], length) == length,
                    "Cannot read a ZIP member name.");
            name.pop_back();
            const auto extension = Extension(name);
            if (extension != ".3mm" && extension != ".vmm")
                continue;
            Require(SafeName(name) && name.size() < sizeof(MarcusMovieEntry::name),
                    "Unsafe or overlong movie name in ZIP.");
            Require(opened->members.size() < 10000, "A ZIP may contain at most 10,000 movie entries.");
            Member member;
            member.name = std::move(name);
            member.index = index;
            member.crc = stat.m_crc32;
            member.bytes = stat.m_uncomp_size;
            member.method = stat.m_method;
            member.encrypted = stat.m_is_encrypted != 0;
            member.supported = stat.m_is_supported != 0;
            opened->members.push_back(std::move(member));
        }
        Require(!opened->members.empty(), "This ZIP contains no .3MM or .VMM movies.");
    }
    impl = std::move(opened);
    prepared.reset();
}
int MarcusMovieInput::Count() const
{
    return static_cast<int>(impl->members.size());
}
MarcusMovieEntry MarcusMovieInput::Entry(int index) const
{
    Require(index >= 0 && index < Count(), "Movie entry is outside the ZIP list.");
    const auto &member = impl->members[index];
    MarcusMovieEntry result = {};
    std::memcpy(result.name, member.name.data(), std::min(member.name.size(), sizeof(result.name) - 1));
    result.bytes = static_cast<double>(member.bytes);
    result.zipped = impl->zipped;
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusMovieInput::Materialize(int index, const std::function<bool()> &cancelled)
{
    Require(!cancelled || !cancelled(), "Movie preparation cancelled.");
    Require(index >= 0 && index < Count(), "Select a movie from this ZIP before opening it.");
    auto &member = impl->members[index];
    if (auto existing = member.extracted.lock())
        return existing;
    auto selected = std::make_shared<MarcusMovieFile>(impl->zipped ? fs::path{} : impl->path);
    if (impl->zipped)
    {
        Require(!member.encrypted, "Encrypted ZIP movie entries are not supported.");
        Require(member.bytes && member.bytes <= movieBudget, "A ZIP movie must be nonempty and at most 512 MiB.");
        for (int attempt = 0; attempt < 16 && selected->directory.empty(); ++attempt)
        {
            const auto candidate =
                fs::temp_directory_path() / ("marcus-movie-" + std::to_string(std::random_device{}()));
            if (fs::create_directory(candidate))
                selected->directory = candidate;
        }
        Require(!selected->directory.empty(), "Cannot create a private ZIP movie directory.");
        selected->path = selected->directory / ("movie" + Extension(member.name));
        Sink sink(selected->path, member.bytes, cancelled);
        if (member.supported)
        {
            const auto callback = [](void *context, mz_uint64 offset, const void *data,
                                     std::size_t bytes) -> std::size_t {
                return static_cast<Sink *>(context)->Write(offset, data, bytes);
            };
            Require(mz_zip_reader_extract_to_callback(&impl->zip, member.index, callback, &sink, 0) != 0,
                    "Cannot extract the selected ZIP movie (data or CRC error).");
        }
        else
        {
            Require(std::count_if(impl->members.begin(), impl->members.end(),
                                  [&](const Member &other) { return other.name == member.name; }) == 1,
                    "Legacy-compressed duplicate movie names cannot be selected unambiguously.");
            LegacyExtract(impl->path, member, sink);
        }
        Require(sink.Complete(member.crc), "The extracted movie does not match its ZIP size or CRC.");
    }
    member.extracted = selected;
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
