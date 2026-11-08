/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_pack_runtime.h"
#include "studio_settings.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <set>
namespace StudioPacks
{
namespace
{
namespace fs = std::filesystem;
std::unique_ptr<Store> store;
Preferences active;
std::array<Info, 3> infos;
std::array<Info, 3> external;
StudioContent::Products legacy;
std::basic_string<KauaiSettings::NameChar> Native(const std::string &text)
{
    return {text.begin(), text.end()};
}
std::string Settings(bool write, const std::string &input = {})
{
    auto key = Native("Software\\Microsoft\\Microsoft Kids\\3D Movie Maker"), name = Native("ExpansionPacks");
    auto &backend = StudioSettings::IniBackend();
    KauaiSettings::Request request;
    request.type = KauaiSettings::Type::String;
    request.write = write;
    bool missing = false, wrong = false;
    if (write)
    {
        auto text = Native(input);
        text.push_back(0);
        if (!KauaiSettings::Access(backend, key.c_str(), name.c_str(), text.data(),
                                   std::uint32_t(text.size() * sizeof(text[0])), request))
            throw std::runtime_error("Cannot save expansion settings");
        return input;
    }
    std::vector<KauaiSettings::NameChar> buffer(16384);
    if (!KauaiSettings::Access(backend, key.c_str(), name.c_str(), buffer.data(),
                               std::uint32_t(buffer.size() * sizeof(buffer[0])), request, &missing, &wrong) ||
        wrong)
        throw std::runtime_error("Cannot read expansion settings");
    if (missing)
        return {};
    std::string result;
    for (auto c : buffer)
    {
        if (!c)
            break;
        if (c > 127)
            throw std::runtime_error("Invalid expansion setting text");
        result += char(c);
    }
    return result;
}
std::string Narrow(const StudioContent::Text &text)
{
    return {text.begin(), text.end()};
}
int ProductPack(const StudioContent::Product &product)
{
    auto title = Narrow(product.title);
    auto slash = title.find('/');
    auto index = Identify(title.substr(0, slash));
    return index >= 0 ? index : slash == std::string::npos ? -1 : Identify(title.substr(slash + 1));
}
Info External(const fs::path &contentRoot, std::size_t index)
{
    const auto &def = Definitions()[index];
    for (const auto &folder : {def.sourceTitle, def.sourceFolder})
    {
        auto path = contentRoot / folder;
        if (!fs::is_directory(path))
            continue;
        Info info;
        info.installed = true;
        info.path = path.u8string();
        info.valid = true;
        std::set<std::string> names;
        for (const auto &entry : fs::directory_iterator(path))
        {
            auto name = entry.path().filename().u8string();
            for (auto &c : name)
                if (c >= 'a' && c <= 'z')
                    c += 'A' - 'a';
            if (entry.is_regular_file())
                names.insert(name);
        }
        for (auto name : def.files)
        {
            for (auto &c : name)
                if (c >= 'a' && c <= 'z')
                    c += 'A' - 'a';
            info.valid &= names.count(name) > 0;
        }
        return info;
    }
    return {};
}
} // namespace
Preferences SavedPreferences()
{
    return Parse(Settings(false));
}
void SavePreferences(const Preferences &preferences)
{
    Settings(true, Serialize(preferences));
}
Store &ManagedStore()
{
    if (!store)
        throw std::runtime_error("Expansion manager is not initialized");
    return *store;
}
const Preferences &ActivePreferences()
{
    return active;
}
void Initialize(const StudioContent::Text &contentRoot)
{
    auto overridePath = std::getenv("THREEDMM_PACK_DIR");
    auto root = overridePath && *overridePath ? fs::u8path(overridePath)
                                              : fs::u8path(StudioSettings::IniPath()).parent_path() / "content/packs";
    if (!root.is_absolute())
        throw std::runtime_error("THREEDMM_PACK_DIR must be absolute");
    store.reset(new Store(root));
    auto text = Settings(false);
    active = Parse(text);
    legacy = StudioContent::ReadProducts(contentRoot.c_str());
    if (text.empty() && legacy.status == StudioContent::Status::Ready)
        for (const auto &product : legacy.entries)
        {
            int index = ProductPack(product);
            if (index < 0)
                continue;
            try
            {
                auto text = Narrow(product.sourceId);
                std::size_t end = 0;
                auto id = std::stoi(text, &end);
                if (end == text.size() && id > 0)
                    active[index].sourceId = id;
            }
            catch (...)
            {
            }
        }
    bool changed = text.empty();
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (active[i].remove && !active[i].generation.empty())
        {
            active[i].enabled = false;
            try
            {
                auto kept = store->Remove(Id(i), active[i]);
                if (kept)
                    std::fprintf(stderr, "Expansion removal preserved changed files: %s\n", Definitions()[i].title);
                active[i].generation.clear();
                active[i].remove = false;
                changed = true;
            }
            catch (const std::exception &error)
            {
                std::fprintf(stderr, "Expansion removal pending: %s\n", error.what());
            }
        }
        infos[i] = store->Inspect(Id(i), active[i]);
        external[i] = External(fs::path(contentRoot), i);
    }
    if (changed)
        SavePreferences(active);
}
StudioContent::Products Products()
{
    if (legacy.status != StudioContent::Status::Ready)
        return legacy;
    StudioContent::Products result;
    bool base = false;
    if (legacy.status == StudioContent::Status::Ready)
        for (const auto &product : legacy.entries)
            if (ProductPack(product) < 0)
            {
                result.entries.push_back(product);
                auto title = Narrow(product.title);
                base |= title.find("3D Movie Maker/") == 0 || title.find("3DMovie/") == 0;
            }
    if (!base)
        result.entries.push_back(
            {StudioContent::Text{'2'}, StudioContent::Text{'3', 'D', ' ', 'M', 'o', 'v', 'i', 'e', ' ', 'M', 'a',
                                                           'k', 'e', 'r', '/', '3', 'D', 'M', 'o', 'v', 'i', 'e'}});
    for (std::size_t i = 0; i < 3; ++i)
        if ((infos[i].valid || external[i].valid) && active[i].enabled && !active[i].remove)
        {
            auto id = std::to_string(active[i].sourceId);
            for (const auto &product : result.entries)
                if (Narrow(product.sourceId) == id)
                    throw std::runtime_error("An expansion source ID is already used by another product");
            auto title = std::string(Definitions()[i].sourceTitle) + '/' + Definitions()[i].sourceFolder;
            result.entries.push_back({{id.begin(), id.end()}, {title.begin(), title.end()}});
        }
    bool portable = false;
    for (const auto &product : result.entries)
        if (Narrow(product.sourceId) == "6")
        {
            if (Narrow(product.title) != "Virtual3dmm/v3dmm")
                throw std::runtime_error("Source ID 6 is already assigned to a different product");
            portable = true;
        }
    if (!portable)
    {
        std::string title = "Virtual3dmm/v3dmm";
        result.entries.push_back({StudioContent::Text{'6'}, {title.begin(), title.end()}});
    }
    return result;
}
Access Resolve(const std::string &longName, const std::string &shortName, std::string &path)
{
    int index = Identify(shortName);
    if (index < 0)
        index = Identify(longName);
    if (index < 0)
        return Access::Legacy;
    if (!active[index].enabled && (infos[index].installed || external[index].installed))
        return Access::Disabled;
    if (active[index].remove)
        return Access::Disabled;
    if (infos[index].installed)
    {
        path = infos[index].path;
        return infos[index].valid ? Access::Available : Access::Missing;
    }
    return Access::Legacy;
}
Edits::Edits() : original(SavedPreferences()), values(original)
{
    for (std::size_t i = 0; i < 3; ++i)
        beforeRemoval[i] = values[i].remove ? active[i].enabled : values[i].enabled;
}
Edits::~Edits()
{
    for (auto &stage : staged)
        try
        {
            ManagedStore().Discard(stage);
        }
        catch (const std::exception &error)
        {
            std::fprintf(stderr, "Pack staging preserved: %s\n", error.what());
        }
}
Info Edits::Inspect(std::size_t index) const
{
    if (!staged.at(index).generation.empty())
        return {true, true, staged[index].path.u8string(), {}};
    auto info = ManagedStore().Inspect(Id(index), values.at(index));
    return info.installed ? info : external.at(index);
}
bool Edits::Managed(std::size_t index) const
{
    return !values.at(index).generation.empty() || !staged.at(index).generation.empty();
}
void Edits::Install(std::size_t index, const fs::path &source, const fs::path &baseISO, const fs::path &fixups)
{
    if (Managed(index))
        throw std::runtime_error("This pack already has a managed installation");
    staged.at(index) = fs::is_directory(source) ? ManagedStore().StageFolder(Id(index), source)
                                                : ManagedStore().StageISO(Id(index), source, baseISO, fixups);
    values[index].generation = staged[index].generation;
    values[index].enabled = true;
    values[index].remove = false;
}
void Edits::ToggleRemove(std::size_t index)
{
    if (!staged.at(index).generation.empty())
    {
        ManagedStore().Discard(staged[index]);
        values[index] = original[index];
        return;
    }
    if (!Managed(index))
        throw std::runtime_error("External installations are read-only; the port cannot uninstall them");
    if (values[index].remove)
    {
        values[index].remove = false;
        values[index].enabled = beforeRemoval[index];
    }
    else
    {
        beforeRemoval[index] = values[index].enabled;
        values[index].remove = true;
        values[index].enabled = false;
    }
}
void Edits::Commit()
{
    for (auto &stage : staged)
        if (!stage.generation.empty())
            ManagedStore().Publish(stage);
    SavePreferences(values);
    for (auto &stage : staged)
    {
        stage.path.clear();
        stage.generation.clear();
    }
    original = values;
}
} // namespace StudioPacks
