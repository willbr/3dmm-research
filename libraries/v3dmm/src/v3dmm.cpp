// Licensed under the MIT License.
#include "three_mm/v3dmm.h"
#include "physfs_zip_order.h"
#include <miniz.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
namespace ThreeMM::V3dmm
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
std::uint32_t Integer(Input &input, std::uint64_t &offset, unsigned bytes)
{
    unsigned char buffer[4]{};
    input.Read(offset, buffer, bytes);
    offset += bytes;
    std::uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i)
        value |= std::uint32_t(buffer[i]) << (i * 8);
    return value;
}
void Integer(std::ostream &output, std::uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        output.put(char(value >> (i * 8)));
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
std::string Trim(std::string value)
{
    auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{}
                                      : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
bool SafeName(const std::string &name)
{
    // Initial package support is deliberately flat, like all 609 reference VXPs.
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:\0", 0, 4) == std::string::npos;
}
struct Zip
{
    Input input;
    mz_zip_archive zip{};
    explicit Zip(const Range &range) : input(range)
    {
        zip.m_pIO_opaque = this;
        zip.m_pRead = [](void *context, mz_uint64 offset, void *buffer, size_t length) -> size_t {
            try
            {
                static_cast<Zip *>(context)->input.Read(offset, buffer, length);
                return length;
            }
            catch (...)
            {
                return 0;
            }
        };
        Require(mz_zip_reader_init(&zip, range.length, 0) != 0, "Invalid VXP ZIP archive");
    }
    ~Zip()
    {
        mz_zip_reader_end(&zip);
    }
    std::vector<Member> Members(const Limits &limits)
    {
        auto count = mz_zip_reader_get_num_files(&zip);
        Require(count && count <= limits.members, "Too many or no VXP members");
        std::map<std::string, std::string> names;
        std::vector<Member> result;
        std::uint64_t total = 0;
        for (mz_uint i = 0; i < count; ++i)
        {
            mz_zip_archive_file_stat stat{};
            Require(mz_zip_reader_file_stat(&zip, i, &stat) != 0, "Invalid VXP member record");
            auto length = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
            Require(length > 1 && length <= 65536, "Invalid VXP member name length");
            std::string name(length, '\0');
            Require(mz_zip_reader_get_filename(&zip, i, name.data(), length) == length, "Cannot read VXP member name");
            name.pop_back();
            Require(SafeName(name) && !stat.m_is_directory && !stat.m_is_encrypted && stat.m_is_supported,
                    "Unsupported, unsafe or ambiguous VXP member: " + name);
            const auto prior = names.emplace(Fold(name), name);
            Require(prior.second || prior.first->second == name, "Ambiguous VXP filename casing: " + name);
            Require(stat.m_uncomp_size <= limits.memberBytes && total <= limits.expandedBytes &&
                        stat.m_uncomp_size <= limits.expandedBytes - total,
                    "VXP exceeds its extraction budget");
            total += stat.m_uncomp_size;
            result.push_back({name, stat.m_uncomp_size, i});
        }
        // PhysicsFS accepts repeated exact names, including different bytes.
        // Select its lookup winner once for both CFG parsing and extraction.
        return Detail::SelectZipMembers(result);
    }
    std::string Text(const Member &member)
    {
        Require(member.bytes <= 1024 * 1024, "VXP configuration is too large");
        std::string result(std::size_t(member.bytes), '\0');
        Require(mz_zip_reader_extract_to_mem(&zip, member.index, result.data(), result.size(), 0) != 0,
                "Cannot decode VXP configuration");
        Require(result.find('\0') == std::string::npos, "Invalid VXP configuration text");
        return result;
    }
};
Package Configuration(Zip &zip, const Limits &limits)
{
    Package package;
    package.members = zip.Members(limits);
    const Member *cfg = nullptr;
    for (const auto &member : package.members)
        if (Extension(member.name) == ".cfg")
        {
            Require(!cfg, "VXP has multiple configurations");
            cfg = &member;
        }
    Require(cfg != nullptr, "VXP has no configuration");
    package.shortName = cfg->name.substr(0, cfg->name.rfind('.'));
    std::istringstream text(zip.Text(*cfg));
    std::string line;
    while (std::getline(text, line))
    {
        line = Trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';')
            continue;
        auto equals = line.find('=');
        if (equals != std::string::npos)
            package.metadata[Fold(Trim(line.substr(0, equals)))] = Trim(line.substr(equals + 1));
    }
    package.name = package.metadata["name"];
    package.author = package.metadata["author"];
    Require(!package.name.empty() && package.name.size() <= 65535 && !package.author.empty(),
            "VXP configuration needs Name and Author");
    Require(Fold(package.metadata["type"]) == "portable", "Only Portable VXP packages are supported");
    for (const auto &setting :
         std::map<std::string, std::string>{{"mount point", "v3dmm"}, {"regid", "6"}, {"regname", "virtual3dmm/v3dmm"}})
    {
        auto field = package.metadata.find(setting.first);
        Require(field == package.metadata.end() || Fold(field->second) == setting.second,
                "VXP uses an unsupported portable source override: " + setting.first);
    }
    auto root = Fold(package.shortName);
    for (auto extension : {".3cn", ".3th"})
        Require(std::any_of(package.members.begin(), package.members.end(),
                            [&](const Member &member) { return Fold(member.name) == root + extension; }),
                "VXP has no matching " + std::string(extension));
    return package;
}
} // namespace
std::string Fold(std::string text)
{
    for (auto &c : text)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
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
    char signature[6];
    input.Read(0, signature, sizeof(signature));
    Require(std::string(signature, 6) == std::string("v3dmm\1", 6), "Unsupported VMM signature or version");
    std::uint64_t offset = 6;
    auto count = Integer(input, offset, 4);
    Require(count && count <= limits.dependencies && count <= (input.range.length - offset) / 6,
            "Invalid VMM dependency count");
    Bundle bundle;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        auto length = Integer(input, offset, 4);
        auto nameLength = Integer(input, offset, 2);
        Require(nameLength != 0, "Empty VMM dependency name");
        std::string name(nameLength, '\0');
        input.Read(offset, name.data(), nameLength);
        offset += nameLength;
        Require(name.find('\0') == std::string::npos, "Invalid VMM dependency name");
        bundle.dependencies.push_back({name, {input.range.file, 0, length}});
    }
    for (auto &dependency : bundle.dependencies)
        if (dependency.archive.length)
        {
            Require(Integer(input, offset, 2) == 0x0bf0, "Invalid VMM expansion start marker");
            Require(offset <= input.range.length && dependency.archive.length <= input.range.length - offset,
                    "Truncated embedded VXP");
            dependency.archive.offset = offset;
            offset += dependency.archive.length;
            Require(Integer(input, offset, 2) == 0xebfe, "Invalid VMM expansion end marker");
        }
    char movieMagic[4];
    input.Read(offset, movieMagic, 4);
    Require(std::string(movieMagic, 4) == "CHN2", "VMM contains no chunky movie");
    bundle.movie = {input.range.file, offset, input.range.length - offset};
    return bundle;
}
Package ReadPackage(const Range &archive, const Limits &limits)
{
    Zip zip(archive);
    return Configuration(zip, limits);
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
    auto package = Configuration(zip, limits);
    std::map<std::string, fs::path> result;
    for (const auto &member : package.members)
    {
        auto extension = Extension(member.name);
        if (extension != ".3cn" && extension != ".3th")
            continue;
        auto path = directory / (std::to_string(member.index) + extension);
        Require(!fs::exists(path), "VXP extraction target already exists");
        std::ofstream stream(path, std::ios::binary);
        Require(bool(stream), "Cannot create VXP content file");
        auto callback = [](void *context, mz_uint64, const void *bytes, size_t count) -> size_t {
            auto &out = *static_cast<std::ofstream *>(context);
            out.write(static_cast<const char *>(bytes), std::streamsize(count));
            return out ? count : 0;
        };
        Require(mz_zip_reader_extract_to_callback(&zip.zip, member.index, callback, &stream, 0) != 0,
                "Cannot extract VXP content (data or CRC error)");
        stream.close();
        Require(bool(stream), "Cannot finish VXP content file");
        result.emplace(member.name, path);
    }
    return result;
}
void WriteBundle(const Range &movie, const std::vector<Dependency> &dependencies, const std::filesystem::path &output)
{
    Require(!dependencies.empty() && dependencies.size() <= 10000, "Invalid output dependency count");
    Require(fs::weakly_canonical(movie.file) != fs::weakly_canonical(output), "Cannot overwrite working movie");
    for (const auto &dependency : dependencies)
    {
        Require(!dependency.name.empty() && dependency.name.size() <= 65535 &&
                    dependency.name.find('\0') == std::string::npos && dependency.archive.length <= UINT32_MAX,
                "VMM output dependency exceeds format limits");
        if (dependency.archive.length)
            Require(fs::weakly_canonical(dependency.archive.file) != fs::weakly_canonical(output),
                    "Cannot overwrite an expansion archive");
    }
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    Require(bool(stream), "Cannot create VMM output");
    stream.write("v3dmm\1", 6);
    Integer(stream, std::uint32_t(dependencies.size()), 4);
    for (const auto &dependency : dependencies)
    {
        Integer(stream, std::uint32_t(dependency.archive.length), 4);
        Integer(stream, std::uint32_t(dependency.name.size()), 2);
        stream.write(dependency.name.data(), dependency.name.size());
    }
    for (const auto &dependency : dependencies)
        if (dependency.archive.length)
        {
            Integer(stream, 0x0bf0, 2);
            Transfer(dependency.archive, stream);
            Integer(stream, 0xebfe, 2);
        }
    Transfer(movie, stream);
    stream.close();
    Require(bool(stream), "Cannot finish VMM output");
}
} // namespace ThreeMM::V3dmm
