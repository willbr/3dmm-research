// Licensed under the MIT License.
#include "studio_vmm_content.h"
#include "mm/vmm_dependencies.h"
#include <exception>
#include <stdexcept>

namespace StudioV3dmm
{
namespace
{
namespace V = MM::V3dmm;

struct MoviePackageProvider
{
    const V::Bundle &bundle;
    const std::function<bool(const std::string &)> &libraryAvailable;
    const std::function<Store *()> &openCatalog;
    const PrivatePackage &materialize;
    Store *catalog = nullptr;
    V::Package metadata;
    Installation pending;
    MoviePackages result;
    std::exception_ptr error;

    template<class Action> int Call(Action action) noexcept
    {
        try { return action(); }
        catch (...) { error = std::current_exception(); return 0; }
    }

    static int Library(void *context, std::size_t i)
    {
        auto &self = *static_cast<MoviePackageProvider *>(context);
        return self.Call([&] { return self.libraryAvailable(self.bundle.dependencies[i].name); });
    }
    static int External(void *context, std::size_t i)
    {
        auto &self = *static_cast<MoviePackageProvider *>(context);
        return self.Call([&] {
            self.result.libraries.push_back(self.bundle.dependencies[i].name);
            return 1;
        });
    }
    static int Open(void *context)
    {
        auto &self = *static_cast<MoviePackageProvider *>(context);
        return self.Call([&] {
            self.catalog = self.openCatalog();
            if (!self.catalog && !self.materialize) throw std::runtime_error("Cannot open the VXP catalog");
            return 1;
        });
    }
    static int Installed(void *context, std::size_t slot, MMVmmPackageIdentity *identity)
    {
        const auto &self = *static_cast<MoviePackageProvider *>(context);
        if (!self.catalog || slot >= self.catalog->Entries().size()) return 0;
        const auto &entry = self.catalog->Entries()[slot];
        *identity = {entry.name.c_str(), entry.shortName.c_str()};
        return 1;
    }
    static int Embedded(void *context, std::size_t i, MMVmmPackageIdentity *identity)
    {
        auto &self = *static_cast<MoviePackageProvider *>(context);
        return self.Call([&] {
            self.metadata = V::ReadPackage(self.bundle.dependencies[i].archive);
            *identity = {self.metadata.name.c_str(), self.metadata.shortName.c_str()};
            return 1;
        });
    }
    static int Select(void *context, std::size_t i, std::size_t slot, const char **name)
    {
        auto &self = *static_cast<MoviePackageProvider *>(context);
        return self.Call([&] {
            const auto &dependency = self.bundle.dependencies[i];
            if (slot != MM_VMM_NO_CATALOG_ENTRY)
                self.pending = self.catalog->Resolve(self.catalog->Entries()[slot].hash);
            else if (self.materialize)
                self.pending = self.materialize(dependency, i, self.metadata);
            else
                self.pending = self.catalog->Install(dependency.archive);
            *name = self.pending.name.c_str();
            return 1;
        });
    }
    static const char *KeptName(void *context, std::size_t index)
    {
        return static_cast<MoviePackageProvider *>(context)->result.portable[index].name.c_str();
    }
    static int Keep(void *context)
    {
        auto &self = *static_cast<MoviePackageProvider *>(context);
        return self.Call([&] {
            self.result.portable.push_back(std::move(self.pending));
            return 1;
        });
    }
};

struct PortableInspection
{
    const std::function<void(const std::filesystem::path &)> &validatePath;
    std::exception_ptr error;

    static int Size(void *context, const char *location, MMVfsPosition *size) noexcept
    {
        auto &self = *static_cast<PortableInspection *>(context);
        try
        {
            const auto path = std::filesystem::u8path(location);
            if (self.validatePath) self.validatePath(path);
            const auto bytes = std::filesystem::file_size(path);
            *size = {static_cast<unsigned long>(bytes >> 32), static_cast<unsigned long>(bytes & 0xffffffffU)};
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

MoviePackages ResolveMoviePackages(const V::Bundle &bundle,
                                  const std::function<bool(const std::string &)> &libraryAvailable,
                                  const std::function<Store *()> &openCatalog, const PrivatePackage &materialize)
{
    MoviePackageProvider provider{bundle, libraryAvailable, openCatalog, materialize, nullptr, {}, {}, {}, {}};
    const MMVmmDependencyServices services = {&provider, MoviePackageProvider::Library, MoviePackageProvider::External,
        MoviePackageProvider::Open, MoviePackageProvider::Installed, MoviePackageProvider::Embedded,
        MoviePackageProvider::Select, MoviePackageProvider::KeptName, MoviePackageProvider::Keep};
    std::vector<MMVmmEntry> entries;
    entries.reserve(bundle.dependencies.size());
    for (const auto &dependency : bundle.dependencies)
        entries.push_back({dependency.name.c_str(), dependency.name.size(),
                           static_cast<MMU32>(dependency.archive.length), {}});
    const auto mode = materialize ? MM_VMM_PRIVATE_CONTENT : MM_VMM_WRITABLE_CATALOG;
    std::size_t failed;
    const auto status = mm_vmm_resolve_dependencies(entries.data(), entries.size(), mode, &services, &failed);
    if (provider.error) std::rethrow_exception(provider.error);
    if (status == MM_VMM_LIBRARY_MISSING)
    {
        const auto &name = bundle.dependencies[failed].name;
        if (materialize) throw std::runtime_error("This VMM requires an unavailable or disabled library: " + name);
        throw std::runtime_error("This VMM needs an external expansion that is not installed or enabled: " +
                                 name + ". Use Options > Expansions.");
    }
    if (status == MM_VMM_PACKAGE_NAME_MISMATCH)
        throw std::runtime_error("Embedded VXP name does not match its VMM dependency");
    if (status != MM_VMM_DEPENDENCIES_OK) throw std::runtime_error("Cannot resolve VMM dependencies");
    return std::move(provider.result);
}

void MountMoviePackages(MM::Content::Vfs &filesystem, const std::vector<Installation> &selection,
                        const std::function<void(const std::filesystem::path &)> &validatePath)
{
    std::vector<MM::Content::File> files;
    for (const auto &package : selection)
        for (const auto &member : package.members)
            files.push_back({member.first, member.second});
    PortableInspection inspection{validatePath, {}};
    const auto limit = V::Limits{}.expandedBytes;
    const MMVfsPortableCheck check = {&inspection, PortableInspection::Size,
        {static_cast<unsigned long>(limit >> 32), static_cast<unsigned long>(limit & 0xffffffffU)},
        validatePath ? 1 : 0};
    const int status = filesystem.ReplacePortable(files, check);
    if (inspection.error) std::rethrow_exception(inspection.error);
    if (status == MM_VFS_BUDGET)
        throw std::runtime_error(validatePath ? "Active VXP content exceeds its disk budget"
                                               : "Applied portable content exceeds its disk budget");
    if (status != MM_VFS_OK)
        throw std::runtime_error(std::string(mm_vfs_error(status)) + " in v3dmm");
}
} // namespace StudioV3dmm
