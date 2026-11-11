// Licensed under the MIT License.
#include "studio_vxp_store.h"
#include "pack_hash.h"
#include "three_mm/archive.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <random>
#include <stdexcept>
#include <sqlite3.h>
#include <set>
namespace StudioV3dmm
{
namespace
{
namespace fs = std::filesystem;
namespace V = ThreeMM::V3dmm;
using Json = nlohmann::json;
void Require(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}
fs::path Checked(const fs::path &root, const std::string &hash)
{
    Require(hash.size() == 64 && std::all_of(hash.begin(), hash.end(),
                                             [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }),
            "Invalid VXP installation identity");
    auto path = root / hash;
    Require(!fs::is_symlink(path), "VXP installation is a symbolic link");
    Require(fs::weakly_canonical(path).parent_path() == fs::canonical(root), "VXP installation escaped its store");
    return path;
}
void Validate(const fs::path &path)
{
    ThreeMM::Options options;
    options.maxDecodedBytes = 256u * 1024 * 1024;
    ThreeMM::Archive archive(options);
    ThreeMM::Error error;
    Require(archive.Open(path.u8string(), error) && !archive.Chunks().empty(), "Invalid VXP chunky content");
    for (std::size_t i = 0; i < archive.Chunks().size(); ++i)
    {
        ThreeMM::PayloadPage payload;
        Require(archive.ReadPayload(i, 0, 1, payload, error), "Invalid compressed VXP content");
        for (const auto &child : archive.Chunks()[i].children)
            Require(child.present, "VXP content has a missing child");
    }
}
} // namespace
namespace
{
struct CachedFile
{
    std::string name, filename, hash, stamp;
    std::uintmax_t bytes = 0;
    bool verified = false;
};
struct Metadata
{
    Installation entry;
    std::vector<CachedFile> files;
};
bool HashIdentity(const std::string &hash)
{
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(),
                                            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
void SqlCheck(int code, sqlite3 *db)
{
    if (code != SQLITE_OK)
        throw std::runtime_error(std::string("VXP catalog: ") + sqlite3_errmsg(db));
}
void Sql(sqlite3 *db, const char *sql)
{
    SqlCheck(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), db);
}
struct Statement
{
    sqlite3 *db;
    sqlite3_stmt *value = nullptr;
    Statement(sqlite3 *db, const char *sql) : db(db)
    {
        SqlCheck(sqlite3_prepare_v2(db, sql, -1, &value, nullptr), db);
    }
    ~Statement()
    {
        sqlite3_finalize(value);
    }
    void Bind(int index, const std::string &text)
    {
        SqlCheck(sqlite3_bind_text(value, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT), db);
    }
    void Bind(int index, sqlite3_int64 number)
    {
        SqlCheck(sqlite3_bind_int64(value, index, number), db);
    }
    bool Row()
    {
        const int code = sqlite3_step(value);
        if (code == SQLITE_ROW)
            return true;
        if (code == SQLITE_DONE)
            return false;
        SqlCheck(code, db);
        return false;
    }
    void Done()
    {
        Require(!Row(), "Unexpected VXP catalog result");
    }
    std::string Text(int column) const
    {
        const auto bytes = sqlite3_column_text(value, column);
        return bytes ? std::string(reinterpret_cast<const char *>(bytes), sqlite3_column_bytes(value, column))
                     : std::string();
    }
    sqlite3_int64 Number(int column) const
    {
        return sqlite3_column_int64(value, column);
    }
};
struct Transaction
{
    sqlite3 *db;
    bool committed = false;
    explicit Transaction(sqlite3 *db, bool write = true) : db(db)
    {
        Sql(db, write ? "BEGIN IMMEDIATE" : "BEGIN");
    }
    ~Transaction()
    {
        if (!committed)
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    void Commit()
    {
        Sql(db, "COMMIT");
        committed = true;
    }
};
Metadata ReadMetadata(const fs::path &root, const std::string &hash, bool enabled)
{
    const auto directory = Checked(root, hash);
    const auto archive = directory / "package.vxp";
    Require(!fs::is_symlink(archive), "Installed VXP archive is a symbolic link");
    const auto range = V::WholeFile(archive);
    const auto package = V::ReadPackage(range);
    const auto manifestPath = directory / "content.json";
    Require(!fs::is_symlink(manifestPath) && fs::file_size(manifestPath) <= 4 * 1024 * 1024,
            "Invalid VXP content manifest");
    std::ifstream stream(manifestPath);
    const auto manifest = Json::parse(stream);
    Require(manifest.is_object(), "Invalid VXP content hashes");
    Metadata result;
    result.entry = {package.name, package.author, hash, enabled, archive, {}};
    result.entry.shortName = package.shortName;
    result.files.push_back({"", "package.vxp", hash, "", range.length, false});
    for (const auto &member : package.members)
    {
        const auto extension = V::Extension(member.name);
        if (extension != ".3cn" && extension != ".3th")
            continue;
        const auto filename = std::to_string(member.index) + extension;
        const auto expected = manifest.at(filename).get<std::string>();
        Require(HashIdentity(expected), "Invalid installed VXP content hash");
        result.files.push_back({member.name, filename, expected, "", member.bytes, false});
        result.entry.members.emplace(member.name, directory / filename);
    }
    return result;
}
std::string Stamp(const fs::path &path)
{
    return std::to_string(fs::last_write_time(path).time_since_epoch().count());
}
bool VerifyFiles(const fs::path &root, const std::string &hash, std::vector<CachedFile> &files, bool force)
{
    const auto directory = Checked(root, hash);
    bool changed = false;
    Require(!files.empty(), "Installed VXP has no cached files");
    for (auto &file : files)
    {
        const auto filename = fs::u8path(file.filename);
        Require(!file.filename.empty() && filename == filename.filename() && file.filename != "." &&
                    file.filename != ".." && HashIdentity(file.hash),
                "Invalid VXP cached filename or hash");
        const auto path = directory / filename;
        Require(!fs::is_symlink(path) && fs::is_regular_file(path) && fs::file_size(path) == file.bytes,
                "Installed VXP content is missing or changed: " + path.u8string());
        const auto stamp = Stamp(path);
        if (force || !file.verified || stamp != file.stamp)
        {
            Require(StudioPacks::HashFile(path) == file.hash, "Installed VXP content has changed: " + path.u8string());
            Require(fs::file_size(path) == file.bytes && Stamp(path) == stamp,
                    "Installed VXP changed during verification");
            file.stamp = stamp;
            file.verified = true;
            changed = true;
        }
    }
    return changed;
}
} // namespace

struct Store::Catalog
{
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db{nullptr, sqlite3_close};
    explicit Catalog(const fs::path &root)
    {
        const auto path = root / "catalog.sqlite3";
        Require(!fs::is_symlink(path), "VXP catalog is a symbolic link");
        sqlite3 *opened = nullptr;
        const int code = sqlite3_open_v2(path.u8string().c_str(), &opened,
                                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
        db.reset(opened);
        SqlCheck(code, db.get());
        SqlCheck(sqlite3_busy_timeout(db.get(), 10000), db.get());
        Sql(db.get(), "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;");
        Sql(db.get(), "CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
                      "CREATE TABLE IF NOT EXISTS packages(hash TEXT PRIMARY KEY,position INTEGER NOT NULL,name TEXT "
                      "NOT NULL,folded TEXT NOT NULL UNIQUE,author TEXT NOT NULL,shortname TEXT NOT NULL,enabled "
                      "INTEGER NOT NULL,error TEXT NOT NULL);"
                      "CREATE TABLE IF NOT EXISTS files(package TEXT NOT NULL REFERENCES packages(hash) ON DELETE "
                      "CASCADE,name TEXT NOT NULL,filename TEXT NOT NULL,bytes INTEGER NOT NULL,hash TEXT NOT "
                      "NULL,stamp TEXT NOT NULL,verified INTEGER NOT NULL,PRIMARY KEY(package,name));");
    }
    bool Ready()
    {
        Statement query(db.get(), "SELECT value FROM metadata WHERE key='schema'");
        if (!query.Row())
            return false;
        Require(query.Text(0) == "1", "Unsupported VXP catalog schema");
        return true;
    }
    void Put(const Metadata &metadata, sqlite3_int64 position)
    {
        const auto &entry = metadata.entry;
        Statement package(db.get(),
                          "INSERT INTO packages VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(hash) DO UPDATE SET "
                          "position=excluded.position,name=excluded.name,folded=excluded.folded,author=excluded.author,"
                          "shortname=excluded.shortname,enabled=excluded.enabled,error=excluded.error");
        package.Bind(1, entry.hash);
        package.Bind(2, position);
        package.Bind(3, entry.name);
        package.Bind(4, V::Fold(entry.name));
        package.Bind(5, entry.author);
        package.Bind(6, entry.shortName);
        package.Bind(7, sqlite3_int64(entry.enabled));
        package.Bind(8, entry.error);
        package.Done();
        Statement clear(db.get(), "DELETE FROM files WHERE package=?");
        clear.Bind(1, entry.hash);
        clear.Done();
        for (const auto &file : metadata.files)
        {
            Statement insert(db.get(), "INSERT INTO files VALUES(?,?,?,?,?,?,?)");
            insert.Bind(1, entry.hash);
            insert.Bind(2, file.name);
            insert.Bind(3, file.filename);
            insert.Bind(4, sqlite3_int64(file.bytes));
            insert.Bind(5, file.hash);
            insert.Bind(6, file.stamp);
            insert.Bind(7, sqlite3_int64(file.verified));
            insert.Done();
        }
    }
    std::vector<Installation> Load(const fs::path &root)
    {
        Transaction snapshot(db.get(), false);
        std::vector<Installation> entries;
        std::map<std::string, std::size_t> indices;
        Statement packages(db.get(),
                           "SELECT hash,name,author,shortname,enabled,error FROM packages ORDER BY position,rowid");
        while (packages.Row())
        {
            const auto hash = packages.Text(0);
            Require(HashIdentity(hash) && entries.size() < 10000, "Invalid VXP catalog entry");
            Installation entry{
                packages.Text(1), packages.Text(2), hash, packages.Number(4) != 0, root / hash / "package.vxp", {}};
            entry.shortName = packages.Text(3);
            entry.error = packages.Text(5);
            indices[hash] = entries.size();
            entries.push_back(std::move(entry));
        }
        Statement files(db.get(), "SELECT package,name,filename FROM files WHERE name<>'' ORDER BY package,name");
        while (files.Row())
        {
            const auto hash = files.Text(0), filename = files.Text(2);
            Require(indices.count(hash) && fs::u8path(filename) == fs::u8path(filename).filename() && filename != "." &&
                        filename != "..",
                    "Invalid VXP catalog member");
            entries[indices.at(hash)].members.emplace(files.Text(1), root / hash / fs::u8path(filename));
        }
        snapshot.Commit();
        return entries;
    }
    std::vector<CachedFile> Files(const std::string &hash)
    {
        std::vector<CachedFile> files;
        Statement query(db.get(),
                        "SELECT name,filename,bytes,hash,stamp,verified FROM files WHERE package=? ORDER BY name");
        query.Bind(1, hash);
        while (query.Row())
        {
            Require(query.Number(2) >= 0 && files.size() <= V::Limits{}.members, "Invalid cached VXP size/count");
            files.push_back({query.Text(0), query.Text(1), query.Text(3), query.Text(4),
                             static_cast<std::uintmax_t>(query.Number(2)), query.Number(5) != 0});
        }
        return files;
    }
    void Verified(const std::string &hash, const std::vector<CachedFile> &files)
    {
        Transaction transaction(db.get());
        for (const auto &file : files)
        {
            Statement update(db.get(),
                             "UPDATE files SET stamp=?,verified=1 WHERE package=? AND name=? AND hash=? AND bytes=?");
            update.Bind(1, file.stamp);
            update.Bind(2, hash);
            update.Bind(3, file.name);
            update.Bind(4, file.hash);
            update.Bind(5, sqlite3_int64(file.bytes));
            update.Done();
            Require(sqlite3_changes(db.get()) == 1, "VXP catalog changed during verification");
        }
        transaction.Commit();
    }
};
Store::Store(std::filesystem::path directory) : root(fs::absolute(std::move(directory)))
{
    fs::create_directories(root);
    root = fs::canonical(root);
    catalog = std::make_unique<Catalog>(root);
    if (!catalog->Ready())
    {
        Transaction migration(catalog->db.get());
        if (!catalog->Ready())
        {
            const auto legacy = root / "catalog.json";
            if (fs::exists(legacy))
            {
                Require(!fs::is_symlink(legacy) && fs::file_size(legacy) <= 4 * 1024 * 1024, "Invalid VXP catalog");
                std::ifstream stream(legacy);
                const auto rows = Json::parse(stream);
                Require(rows.is_array() && rows.size() <= 10000, "Invalid VXP catalog entries");
                sqlite3_int64 position = 0;
                std::set<std::string> hashes;
                for (const auto &row : rows)
                {
                    const auto hash = row.at("hash").get<std::string>();
                    Require(HashIdentity(hash), "Invalid VXP installation identity");
                    Require(hashes.insert(hash).second, "Duplicate VXP catalog identity");
                    const auto enabled = row.at("enabled").get<bool>();
                    Metadata metadata;
                    try
                    {
                        metadata = ReadMetadata(root, hash, enabled);
                    }
                    catch (const std::exception &error)
                    {
                        metadata.entry = {hash, "", hash, enabled, root / hash / "package.vxp", {}};
                        metadata.entry.error = error.what();
                        // Keep a readable package's name even if its manifest is broken.
                        try
                        {
                            const auto package = V::ReadPackage(V::WholeFile(metadata.entry.archive));
                            metadata.entry.name = package.name;
                            metadata.entry.shortName = package.shortName;
                        }
                        catch (const std::exception &)
                        {
                        }
                    }
                    catalog->Put(metadata, position++);
                }
            }
            Sql(catalog->db.get(), "INSERT INTO metadata VALUES('schema','1')");
        }
        migration.Commit();
    }
    entries = catalog->Load(root);
}
Store::~Store() = default;
Installation Store::Resolve(const std::string &hash, bool forceValidation)
{
    const auto found =
        std::find_if(entries.begin(), entries.end(), [&](const Installation &entry) { return entry.hash == hash; });
    Require(found != entries.end(), "VXP is not installed");
    if (!found->error.empty())
    {
        // A failed legacy metadata import must not permanently poison a package
        // that has since been repaired or reinstalled at its original location.
        auto metadata = ReadMetadata(root, hash, found->enabled);
        VerifyFiles(root, hash, metadata.files, true);
        Transaction transaction(catalog->db.get());
        Statement position(catalog->db.get(), "SELECT position,enabled FROM packages WHERE hash=?");
        position.Bind(1, hash);
        Require(position.Row(), "VXP is not installed");
        metadata.entry.enabled = position.Number(1) != 0;
        catalog->Put(metadata, position.Number(0));
        transaction.Commit();
        *found = metadata.entry;
        return metadata.entry;
    }
    auto files = catalog->Files(hash);
    if (VerifyFiles(root, hash, files, forceValidation))
        catalog->Verified(hash, files);
    return *found;
}
Installation Store::Install(const ThreeMM::V3dmm::Range &source)
{
    auto package = V::ReadPackage(source);
    auto stage = root / (".pending-" + std::to_string(std::random_device{}()));
    Require(fs::create_directory(stage), "Cannot create VXP staging directory");
    std::vector<fs::path> owned;
    try
    {
        auto archive = stage / "package.vxp";
        owned.push_back(archive);
        V::Copy(source, archive);
        package = V::ReadPackage(V::WholeFile(archive));
        auto hash = StudioPacks::HashFile(archive);
        for (const auto &entry : entries)
            if (V::Fold(entry.name) == V::Fold(package.name))
            {
                Require(entry.hash == hash, "A different VXP with this name is already installed: " + package.name);
                fs::remove(archive);
                fs::remove(stage);
                return Resolve(entry.hash);
            }
        auto destination = Checked(root, hash);
        if (!fs::exists(destination))
        {
            // Record every potential member before extraction, including partial writes.
            for (const auto &member : package.members)
            {
                auto ext = V::Extension(member.name);
                if (ext == ".3cn" || ext == ".3th")
                    owned.push_back(stage / (std::to_string(member.index) + ext));
            }
            auto files = V::Materialize(V::WholeFile(archive), stage);
            Json manifest = Json::object();
            for (const auto &file : files)
            {
                Validate(file.second);
                manifest[file.second.filename().u8string()] = StudioPacks::HashFile(file.second);
            }
            owned.push_back(stage / "content.json");
            std::ofstream record(owned.back(), std::ios::binary);
            record << manifest.dump(2) << '\n';
            record.close();
            Require(bool(record), "Cannot save VXP content hashes");
            fs::rename(stage, destination);
            owned.clear();
        }
        else
        {
            fs::remove(archive);
            fs::remove(stage);
        }
        auto metadata = ReadMetadata(root, hash, true);
        VerifyFiles(root, hash, metadata.files, true);
        Transaction transaction(catalog->db.get());
        Statement next(catalog->db.get(), "SELECT COALESCE(MAX(position),-1)+1 FROM packages");
        Require(next.Row(), "Cannot allocate VXP catalog position");
        const auto position = next.Number(0);
        catalog->Put(metadata, position);
        transaction.Commit();
        entries = catalog->Load(root);
        return metadata.entry;
    }
    catch (...)
    {
        // No recursive deletion; preserve anything not created by this import.
        for (const auto &path : owned)
        {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
        std::error_code ignored;
        if (fs::is_directory(stage, ignored) && fs::is_empty(stage, ignored))
            fs::remove(stage, ignored);
        throw;
    }
}
void Store::SetEnabled(const std::string &hash, bool enabled)
{
    Transaction transaction(catalog->db.get());
    Statement update(catalog->db.get(), "UPDATE packages SET enabled=? WHERE hash=?");
    update.Bind(1, sqlite3_int64(enabled));
    update.Bind(2, hash);
    update.Done();
    Require(sqlite3_changes(catalog->db.get()) == 1, "VXP is not installed");
    transaction.Commit();
    entries = catalog->Load(root);
}
} // namespace StudioV3dmm
