// Licensed under the MIT License.
#include "mm/v3dmm.h"
#include "mm/vmm_dependencies.h"
#include "mm/vmm_wire.h"
#include "mm/vxp_zip.h"
#include <algorithm>
#include <array>
#include <exception>
#include <fstream>
#include <stdexcept>
namespace MM::V3dmm
{
namespace
{
namespace fs = std::filesystem;
void Require(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}
class Input
{
  public:
    Range range;
    std::ifstream stream;
    explicit Input(Range source) : range(std::move(source)), stream(range.file, std::ios::binary)
    {
        auto length = fs::file_size(range.file);
        Require(bool(stream) && range.offset <= length && range.length <= length - range.offset,
                "Invalid v3dmm file range");
    }
    void Read(std::uint64_t offset, void *output, std::size_t length)
    {
        Require(offset <= range.length && length <= range.length - offset, "Truncated v3dmm data");
        stream.clear();
        stream.seekg(std::streamoff(range.offset + offset));
        Require(bool(stream.read(static_cast<char *>(output), std::streamsize(length))), "Cannot read v3dmm data");
    }
};
std::uint64_t Position(MMWide offset)
{
    return (std::uint64_t(offset.high) << 32) | offset.low;
}
void Transfer(const Range &range, std::ostream &output)
{
    Input input(range);
    std::array<char, 64 * 1024> buffer;
    for (std::uint64_t offset = 0; offset < range.length;)
    {
        auto count = std::size_t(std::min<std::uint64_t>(buffer.size(), range.length - offset));
        input.Read(offset, buffer.data(), count);
        Require(bool(output.write(buffer.data(), count)), "Cannot write v3dmm data");
        offset += count;
    }
}

struct BundleOutput
{
    const Range &movie;
    const std::vector<Dependency> &dependencies;
    const fs::path &destination;
    std::ofstream stream;
    std::exception_ptr error;

    template<class Action> int Attempt(Action action) noexcept
    {
        try { action(); return 1; }
        catch (...) { error = std::current_exception(); return 0; }
    }
    static int Begin(void *context)
    {
        auto &out = *static_cast<BundleOutput *>(context);
        return out.Attempt([&] {
            out.stream.open(out.destination, std::ios::binary | std::ios::trunc);
            Require(bool(out.stream), "Cannot create VMM output");
        });
    }
    static int Write(void *context, const void *bytes, std::size_t count)
    {
        auto &out = *static_cast<BundleOutput *>(context);
        return out.Attempt([&] {
            Require(bool(out.stream.write(static_cast<const char *>(bytes), std::streamsize(count))),
                    "Cannot write v3dmm data");
        });
    }
    static int CopyArchive(void *context, std::size_t entry)
    {
        auto &out = *static_cast<BundleOutput *>(context);
        return out.Attempt([&] { Transfer(out.dependencies[entry].archive, out.stream); });
    }
    static int CopyMovie(void *context)
    {
        auto &out = *static_cast<BundleOutput *>(context);
        return out.Attempt([&] { Transfer(out.movie, out.stream); });
    }
};

MMVxpSize PackageSize(std::uint64_t bytes)
{
    return {static_cast<MMU32>(bytes), static_cast<MMU32>(bytes >> 32)};
}
std::uint64_t PackageSize(MMVxpSize bytes)
{
    return (std::uint64_t(bytes.high) << 32) | bytes.low;
}

struct Zip
{
    Input input;
    MMVxpZip *zip = nullptr;
    explicit Zip(const Range &range) : input(range)
    {
        const MMVxpZipInput source{&input, PackageSize(range.length),
            [](void *context, MMVxpSize offset, void *bytes, std::size_t length) -> int {
                try { static_cast<Input *>(context)->Read(PackageSize(offset), bytes, length); return 1; }
                catch (...) { return 0; }
            }};
        const auto status = mm_vxp_zip_open(&source, &zip);
        Require(status == MM_VXP_OK, mm_vxp_error(status));
    }
    ~Zip()
    {
        mm_vxp_zip_close(zip);
    }
    Zip(const Zip &) = delete;
    Zip &operator=(const Zip &) = delete;

    std::string MemberName(MMU32 index) const
    {
        const auto length = mm_vxp_zip_member_name(zip, index, nullptr, 0);
        std::string name(length, '\0');
        if (length && mm_vxp_zip_member_name(zip, index, name.data(), length) == length) name.pop_back();
        return name;
    }
};

struct PackageContents
{
    MMVxpPackage value{};
    PackageContents(Zip &zip, const Limits &limits)
    {
        const MMVxpLimits budget = {limits.members, PackageSize(limits.memberBytes), PackageSize(limits.expandedBytes)};
        MMU32 failed;
        const auto status = mm_vxp_zip_package(zip.zip, &budget, &value, &failed);
        if (status == MM_VXP_OK) return;
        std::string message = mm_vxp_error(status);
        const auto name = zip.MemberName(failed);
        if (!name.empty()) message += ": " + name;
        throw std::runtime_error(message);
    }
    ~PackageContents() { mm_vxp_dispose(&value); }
    PackageContents(const PackageContents &) = delete;
    PackageContents &operator=(const PackageContents &) = delete;
};

struct StoryboardInspection
{
    const std::vector<Dependency> &dependencies;
    std::exception_ptr error;
    static int Inspect(void *context, std::size_t slot, int *storyboard)
    {
        auto &self = *static_cast<StoryboardInspection *>(context);
        try
        {
            Zip zip(self.dependencies[slot].archive);
            const PackageContents contents(zip, Limits{});
            *storyboard = mm_vxp_is_storyboard_package(&contents.value);
            return 1;
        }
        catch (...)
        {
            self.error = std::current_exception();
            return 0;
        }
    }
};
} // namespace
std::string Fold(std::string text)
{
    mm_vxp_fold_ascii(text.data(), text.size());
    return text;
}
std::string Extension(const std::string &name)
{
    auto dot = name.rfind('.');
    return dot == std::string::npos ? std::string{} : Fold(name.substr(dot));
}
Range WholeFile(const std::filesystem::path &path)
{
    return {fs::absolute(path), 0, fs::file_size(path)};
}
Bundle ReadBundle(const std::filesystem::path &path, const Limits &limits)
{
    Input input(WholeFile(path));
    const auto length = input.range.length;
    Require(length <= INT64_MAX, "Invalid v3dmm file range");
    const MMVmmReader reader = {&input, {static_cast<MMU32>(length), static_cast<MMI32>(length >> 32)},
        [](void *context, MMWide offset, void *bytes, std::size_t count) {
            try { static_cast<Input *>(context)->Read(Position(offset), bytes, count); return 1; }
            catch (...) { return 0; }
        }};
    struct Directory
    {
        MMVmmDirectory value{};
        ~Directory() { mm_vmm_dispose(&value); }
    } directory;
    const auto status = mm_vmm_read(&reader, limits.dependencies, &directory.value);
    Require(status == MM_VMM_OK, mm_vmm_error(status));
    Bundle bundle;
    bundle.dependencies.reserve(directory.value.count);
    for (std::size_t i = 0; i < directory.value.count; ++i)
    {
        const auto &entry = directory.value.entries[i];
        bundle.dependencies.push_back({std::string(entry.name, entry.name_length),
            {input.range.file, Position(entry.archive_offset), entry.archive_length}});
    }
    bundle.movie = {input.range.file, Position(directory.value.movie_offset), Position(directory.value.movie_length)};
    return bundle;
}
Package ReadPackage(const Range &archive, const Limits &limits)
{
    Zip zip(archive);
    const PackageContents contents(zip, limits);
    const auto &native = contents.value;
    Package package;
    package.name = native.name;
    package.author = native.author;
    package.shortName = native.short_name;
    for (std::size_t i = 0; i < native.field_count; ++i)
        package.metadata.emplace(native.fields[i].key, native.fields[i].value);
    package.members.reserve(native.member_count);
    for (std::size_t i = 0; i < native.member_count; ++i)
    {
        const auto &member = native.members[i];
        package.members.push_back({member.name, PackageSize(member.bytes), member.index});
    }
    return package;
}
void Copy(const Range &range, const std::filesystem::path &output)
{
    Require(fs::weakly_canonical(range.file) != fs::weakly_canonical(output), "Cannot overwrite v3dmm source data");
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    Require(bool(stream), "Cannot create v3dmm output");
    Transfer(range, stream);
    stream.close();
    Require(bool(stream), "Cannot finish v3dmm output");
}
std::map<std::string, std::filesystem::path> Materialize(const Range &archive, const std::filesystem::path &directory,
                                                         const Limits &limits)
{
    Zip zip(archive);
    const PackageContents contents(zip, limits);
    struct Output
    {
        MMVxpZip *zip;
        const fs::path &directory;
        std::map<std::string, fs::path> result;
        std::exception_ptr error;
        static int Sink(void *context, const void *bytes, size_t count)
        {
            try
            {
                auto &out = *static_cast<std::ofstream *>(context);
                return bool(out.write(static_cast<const char *>(bytes), std::streamsize(count)));
            }
            catch (...) { return 0; }
        }
        static int Write(void *context, const MMVxpMember *member, const char *filename)
        {
            auto &out = *static_cast<Output *>(context);
            try
            {
                auto path = out.directory / filename;
                Require(!fs::exists(path), "VXP extraction target already exists");
                std::ofstream stream(path, std::ios::binary);
                Require(bool(stream), "Cannot create VXP content file");
                Require(mm_vxp_zip_extract(out.zip, member->index, Sink, &stream) != 0,
                        "Cannot extract VXP content (data or CRC error)");
                stream.close();
                Require(bool(stream), "Cannot finish VXP content file");
                out.result.emplace(member->name, path);
                return 1;
            }
            catch (...) { out.error = std::current_exception(); return 0; }
        }
    } out{zip.zip, directory, {}, {}};
    if (!mm_vxp_materialize(&contents.value, Output::Write, &out))
        std::rethrow_exception(out.error);
    return std::move(out.result);
}

void WriteBundle(const Range &movie, const std::vector<Dependency> &dependencies, const std::filesystem::path &output)
{
    Require(!dependencies.empty() && dependencies.size() <= MM_VMM_OUTPUT_ENTRY_LIMIT,
            mm_vmm_error(MM_VMM_OUTPUT_COUNT));
    const auto canonical = fs::weakly_canonical(output);
    Require(fs::weakly_canonical(movie.file) != canonical, "Cannot overwrite working movie");
    std::vector<MMVmmEntry> entries;
    entries.reserve(dependencies.size());
    for (const auto &dependency : dependencies)
    {
        Require(dependency.archive.length <= UINT32_MAX, mm_vmm_error(MM_VMM_OUTPUT_ENTRY));
        if (dependency.archive.length)
            Require(fs::weakly_canonical(dependency.archive.file) != canonical, "Cannot overwrite an expansion archive");
        entries.push_back({dependency.name.data(), dependency.name.size(), static_cast<MMU32>(dependency.archive.length), {}});
    }
    BundleOutput writer{movie, dependencies, output, {}, {}};
    const MMVmmWriter output_stream = {&writer, BundleOutput::Begin, BundleOutput::Write,
        BundleOutput::CopyArchive, BundleOutput::CopyMovie};
    const auto status = mm_vmm_write(&output_stream, entries.data(), entries.size());
    if (writer.error) std::rethrow_exception(writer.error);
    Require(status == MM_VMM_OK, mm_vmm_error(status));
    writer.stream.close();
    Require(bool(writer.stream), "Cannot finish VMM output");
}

void WriteStoryboardBundle(const Range &movie, const std::vector<Dependency> &dependencies, const Dependency &replacement,
                           const std::filesystem::path &output)
{
    StoryboardInspection inspection{dependencies, {}};
    std::vector<MMVmmEntry> entries;
    entries.reserve(dependencies.size());
    for (const auto &dependency : dependencies)
        entries.push_back({dependency.name.c_str(), dependency.name.size(), dependency.archive.length ? 1U : 0U, {}});
    std::vector<std::size_t> slots(entries.size() + 1);
    std::size_t selected;
    const auto status = mm_vmm_save_storyboard_dependencies(entries.data(), entries.size(), StoryboardInspection::Inspect,
                                                            &inspection, slots.data(), &selected);
    if (inspection.error) std::rethrow_exception(inspection.error);
    Require(status == MM_VMM_DEPENDENCIES_OK, "Cannot plan storyboard dependencies");
    std::vector<Dependency> outputEntries;
    outputEntries.reserve(selected);
    for (std::size_t i = 0; i < selected; ++i)
        outputEntries.push_back(slots[i] == MM_VMM_SAVE_STORYBOARD ? replacement : dependencies[slots[i]]);
    WriteBundle(movie, outputEntries, output);
}
} // namespace MM::V3dmm
