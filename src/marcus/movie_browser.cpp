#include "dialog_host.h"
#include "movie_browser.h"
#include "movie_input.h"
#include "preview_worker.h"
#include "mm/v3dmm.h"
#include <random>
#include <array>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace fs = std::filesystem;
namespace
{
struct Item
{
    std::string name, path, member;
    int entry = -1;
    bool directory = false;
    double bytes = 0;
    fs::file_time_type modified{};
};
struct Listing
{
    std::string location, error;
    std::vector<Item> items;
    std::shared_ptr<MarcusMovieInput> input;
    unsigned generation = 0;
    std::string folder;
};
struct Prepared
{
    std::string key, path, movie, error;
    unsigned serial = 0;
    int entry = 0;
    bool opening = false, calculating = false;
    BrowserPreview preview;
    std::unique_ptr<MarcusMovieInput> input;
    std::shared_ptr<MarcusMovieFile> file;
    std::shared_ptr<MarcusMovieFile> inner;
};
struct Recent { std::string path, member; };
struct Card
{
    BrowserPreview preview;
    SDL_Texture *texture = nullptr;
    Uint64 used = 0;
    std::string calculation;
    ~Card() { dialog_retire_texture(texture); }
};
std::string Lower(std::string value)
{
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
std::string Extension(const std::string &path) { return Lower(fs::u8path(path).extension().u8string()); }
const char *Kind(const Item &item)
{
    const auto extension = Extension(item.member.empty() ? item.path : item.member);
    if (!item.member.empty() && Extension(item.path) == ".zip")
        return extension == ".vmm" ? "VMM / ZIP" : "3MM / ZIP";
    return item.directory ? "FOLDER" : extension == ".zip" ? "ZIP" : extension == ".vmm" ? "VMM" : "3MM";
}
std::string Size(double bytes)
{
    char value[64];
    SDL_snprintf(value, sizeof(value), bytes >= 1048576 ? "%.1f MiB" : "%.0f KiB",
                 bytes / (bytes >= 1048576 ? 1048576 : 1024));
    return value;
}

}

struct MarcusBrowser
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    bool visible = false, recentView = false, grid = true, sortDate = false, loading = false;
    int focus = 0, selected = -1, filter = 0;
    int visibleFirst = 0, visibleLast = 12;
    std::string location, movieFolder, address, search, filename, error, resources, content, statePath, lastPath, lastMember;
    std::vector<std::string> history, favourites;
    std::vector<Recent> recent;
    std::vector<int> filtered;
    Listing listing;
    std::map<std::string, std::unique_ptr<Card>> cards;
    Uint64 selectedAt = 0, previewAt = 0;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable condition;
    bool stopping = false, requested = false, prepareRequested = false, opening = false, calculating = false;
    Item prepareItem;
    bool prepareOpening = false, prepareCalculating = false;
    std::string preparingKey, prepareResources, prepareContent;
    std::atomic<unsigned> prepareSerial{0};
    unsigned prepareRequestSerial = 0;
    std::unique_ptr<Prepared> preparedResult;
    std::string requestedPath;
    std::vector<Recent> requestedRecent;
    unsigned generation = 0;
    std::atomic<unsigned> latest{0};
    std::unique_ptr<Listing> completed;
    MarcusMovieInput *chosen = nullptr;
    int chosenEntry = 0;
    std::string chosenPath;

    MarcusBrowser(SDL_Window *window, SDL_Renderer *renderer) : window(window), renderer(renderer)
    {
        if (auto pref = SDL_GetPrefPath("3DMMForever", "Marcus"))
        {
            statePath = std::string(pref) + "movie-browser.cfg";
            SDL_free(pref);
            Load();
        }
        std::error_code ec;
        // Match SDL Studio's My movies location, including redirected Documents.
        if (const auto documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS))
        {
            const auto directory = fs::u8path(documents) / "3DMMForever";
            fs::create_directories(directory, ec);
            if (!ec && fs::is_directory(directory, ec)) movieFolder = directory.u8string();
        }
        if (movieFolder.empty()) movieFolder = fs::current_path().u8string();
        if (location.empty()) location = movieFolder;
        worker = std::thread([this] { Work(); });
    }
    ~MarcusBrowser()
    {
        try { if (visible) Close(); } catch (...) { }
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; ++latest; ++prepareSerial; }
        condition.notify_one();
        worker.join();
        marcus_input_destroy(chosen);
    }
    void Load()
    {
        std::ifstream file(fs::u8path(statePath));
        std::string kind, value, member;
        while (file >> kind)
        {
            if (kind == "folder" && file >> std::quoted(value)) location = value;
            else if (kind == "movie-folder" && file >> std::quoted(value)) { /* Obsolete development preference. */ }
            else if (kind == "selected" && file >> std::quoted(value) >> std::quoted(member))
            { lastPath = value; lastMember = member; }
            else if (kind == "view") file >> grid >> sortDate >> filter;
            else if (kind == "favourite" && file >> std::quoted(value))
            { if (favourites.size() < 12) favourites.push_back(value); }
            else if (kind == "recent" && file >> std::quoted(value) >> std::quoted(member))
            { if (recent.size() < 24) recent.push_back({value, member}); }
            else break;
        }
        filter = std::clamp(filter, 0, 3);
    }
    void Save()
    {
        if (statePath.empty()) return;
        const auto temporary = fs::u8path(statePath + ".tmp");
        std::ofstream file(temporary, std::ios::trunc);
        file << "folder " << std::quoted(location) << "\nview " << grid << ' ' << sortDate << ' ' << filter << '\n';
        file << "selected " << std::quoted(lastPath) << ' ' << std::quoted(lastMember) << '\n';
        for (const auto &path : favourites) file << "favourite " << std::quoted(path) << '\n';
        for (const auto &item : recent) file << "recent " << std::quoted(item.path) << ' ' << std::quoted(item.member) << '\n';
        file.close();
        if (file)
        {
            const auto from = temporary.u8string();
            SDL_RenamePath(from.c_str(), statePath.c_str());
        }
    }
    void Work()
    {
        for (;;)
        {
            std::string path;
            std::vector<Recent> recents;
            unsigned id, serial = 0;
            Item item;
            bool preparing = false, forOpen = false, forCalculation = false;
            std::string resourcePath, contentPath;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this] { return requested || prepareRequested || stopping; });
                if (stopping) return;
                id = generation;
                if (requested) { path = requestedPath; recents = requestedRecent; requested = false; }
                else
                {
                    preparing = true; item = prepareItem; forOpen = prepareOpening;
                    serial = prepareRequestSerial; resourcePath = prepareResources; contentPath = prepareContent;
                    forCalculation = prepareCalculating; prepareRequested = false;
                }
            }
            if (preparing)
            {
                auto ready = std::make_unique<Prepared>();
                ready->serial = serial; ready->key = Key(item); ready->path = item.path; ready->opening = forOpen; ready->calculating = forCalculation;
                const auto cancelled = [this, serial] { return prepareSerial != serial; };
                try
                {
                    ready->input = std::make_unique<MarcusMovieInput>();
                    ready->input->Open(item.path, cancelled);
                    int index = std::max(0, item.entry);
                    if (!item.member.empty())
                    {
                        index = -1;
                        for (int i = 0; i < ready->input->Count(); ++i)
                            if (item.member == ready->input->Entry(i).name) { index = i; break; }
                        if (index < 0) throw std::runtime_error("This movie is no longer in the ZIP.");
                    }
                    ready->entry = index;
                    ready->file = ready->input->Materialize(index, cancelled);
                    ready->input->prepared = ready->file;
                    ready->movie = ready->file->path.u8string();
                    if (!forOpen && !forCalculation && !cancelled())
                    {
                        std::ifstream header(ready->file->path, std::ios::binary);
                        char signature[6] = {};
                        header.read(signature, sizeof(signature));
                        if (header.gcount() == 6 && std::string(signature, 6) == std::string("v3dmm\1", 6))
                        {
                            // Read only the inner movie. Previewing must not install packs
                            // or invoke engine archive validation on this worker.
                            const auto range = MM::V3dmm::ReadBundle(ready->file->path).movie;
                            if (range.length > 512ull * 1024 * 1024)
                                throw std::runtime_error("The bundled movie exceeds the preview size limit.");
                            ready->inner = std::make_shared<MarcusMovieFile>(fs::path{});
                            for (int attempt = 0; attempt < 16 && ready->inner->directory.empty(); ++attempt)
                            {
                                const auto directory = fs::temp_directory_path() / ("marcus-movie-" + std::to_string(std::random_device{}()));
                                if (fs::create_directory(directory)) ready->inner->directory = directory;
                            }
                            if (ready->inner->directory.empty()) throw std::runtime_error("Cannot create a preview directory.");
                            ready->inner->path = ready->inner->directory / "movie.3mm";
                            std::ifstream source(range.file, std::ios::binary);
                            std::ofstream output(ready->inner->path, std::ios::binary);
                            source.seekg(static_cast<std::streamoff>(range.offset));
                            std::array<char, 65536> bytes;
                            for (auto remaining = range.length; remaining;)
                            {
                                if (cancelled()) throw std::runtime_error("Movie preparation cancelled.");
                                const auto count = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, bytes.size()));
                                source.read(bytes.data(), count); output.write(bytes.data(), count);
                                if (!source || !output) throw std::runtime_error("Cannot read the bundled movie preview.");
                                remaining -= count;
                            }
                            output.close();
                            if (!output) throw std::runtime_error("Cannot finish the bundled movie preview.");
                            ready->movie = ready->inner->path.u8string();
                        }
                    }
                    if (!forOpen && !cancelled()) ready->preview = RunPreviewWorker(ready->movie, resourcePath, cancelled,
                        contentPath, forCalculation, ready->file->fontDirectory.u8string());
                }
                catch (const std::exception &e) { ready->error = e.what(); }
                if (!forOpen)
                {
                    // All preview file handles and temporary files stay on the
                    // worker. Only bounded strings and RGBA pixels reach the UI.
                    ready->input.reset(); ready->file.reset(); ready->inner.reset();
                }
                std::lock_guard<std::mutex> lock(mutex);
                if (!cancelled()) preparedResult = std::move(ready);
                continue;
            }
            auto result = std::make_unique<Listing>();
            result->location = path; result->generation = id;
            try
            {
                if (path.empty())
                {
                    for (const auto &recent : recents)
                    {
                        Item item;
                        item.path = recent.path; item.member = recent.member;
                        item.name = fs::u8path(item.path).filename().u8string();
                        if (!item.member.empty()) item.name = item.member + "  |  " + item.name;
                        std::error_code ec;
                        item.bytes = static_cast<double>(fs::file_size(fs::u8path(item.path), ec));
                        if (ec) item.bytes = 0;
                        result->items.push_back(std::move(item));
                    }
                }
                else if (Extension(path) == ".zip" && !fs::is_directory(fs::u8path(path)))
                {
                    result->input = std::make_shared<MarcusMovieInput>();
                    result->input->Open(path, [this, id] { return latest != id; });
                    for (int i = 0; i < result->input->Count(); ++i)
                    {
                        const auto member = result->input->Entry(i);
                        Item item;
                        item.name = member.name; item.member = member.name; item.path = path;
                        item.entry = i; item.bytes = member.bytes;
                        result->items.push_back(std::move(item));
                    }
                }
                else
                {
                    for (const auto &entry : fs::directory_iterator(fs::u8path(path), fs::directory_options::skip_permission_denied))
                    {
                        if (latest != id) break;
                        std::error_code ec;
                        const bool directory = entry.is_directory(ec);
                        const auto extension = Lower(entry.path().extension().u8string());
                        if (!directory && extension != ".3mm" && extension != ".vmm" && extension != ".zip") continue;
                        Item item;
                        item.name = entry.path().filename().u8string(); item.path = entry.path().u8string();
                        item.directory = directory;
                        if (!directory) { item.bytes = static_cast<double>(entry.file_size(ec)); if (ec) item.bytes = 0; }
                        item.modified = entry.last_write_time(ec);
                        if (!directory && extension == ".zip")
                        {
                            // Inspect the directory only: no extraction while listing.
                            // A sole movie behaves like a loose movie; collections
                            // retain the ZIP card and their separate member browser.
                            try
                            {
                                MarcusMovieInput zip;
                                zip.Open(item.path, [this, id] { return latest != id; });
                                if (zip.Count() == 1)
                                {
                                    const auto movie = zip.Entry(0);
                                    item.member = movie.name; item.entry = 0; item.bytes = movie.bytes;
                                    item.name = fs::u8path(item.member).filename().u8string();
                                }
                            }
                            catch (const std::exception &) { /* Keep unreadable ZIPs browseable for their error message. */ }
                        }
                        result->items.push_back(std::move(item));
                        if (result->items.size() == 20000) { result->error = "Showing the first 20,000 entries. Choose a smaller folder."; break; }
                    }
                }
            }
            catch (const std::exception &e) { result->error = e.what(); }
            if (!path.empty())
            {
                std::error_code ec;
                result->folder = fs::is_directory(fs::u8path(path), ec) ? path : fs::u8path(path).parent_path().u8string();
            }
            std::lock_guard<std::mutex> lock(mutex);
            if (id == generation) completed = std::move(result);
        }
    }
    void Navigate(std::string path, bool remember = true)
    {
        if (remember) history.push_back(recentView ? "" : listing.location);
        recentView = path.empty(); address = path; search.clear(); filename.clear(); error.clear();
        selected = -1; cards.clear(); filtered.clear(); listing = {};
        loading = true; focus = 0;
        if (Extension(path) == ".zip" && filter == 3) filter = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            requestedPath = path; requestedRecent = recent;
            ++generation; latest = generation; completed.reset(); requested = true;
            ++prepareSerial; prepareRequested = false; preparedResult.reset();
            preparingKey.clear(); opening = false; calculating = false;
        }
        condition.notify_one();
    }
    void Refilter()
    {
        filtered.clear();
        const auto query = Lower(search);
        for (int i = 0; i < static_cast<int>(listing.items.size()); ++i)
        {
            const auto &item = listing.items[i];
            const auto ext = Extension(filter == 3 || item.member.empty() ? item.path : item.member);
            if (!item.directory && filter && ext != (filter == 1 ? ".3mm" : filter == 2 ? ".vmm" : ".zip")) continue;
            if (Lower(item.name + " " + fs::u8path(item.path).filename().u8string()).find(query) != std::string::npos)
                filtered.push_back(i);
        }
        if (!recentView)
            std::stable_sort(filtered.begin(), filtered.end(), [this](int a, int b) {
                const auto &left = listing.items[a], &right = listing.items[b];
                if (left.directory != right.directory) return left.directory;
                if (sortDate && left.modified != right.modified) return left.modified > right.modified;
                return Lower(left.name) < Lower(right.name);
            });
        Select(filtered.empty() ? -1 : 0);
    }
    const Item *Selected() const
    {
        return selected >= 0 && selected < static_cast<int>(filtered.size()) ? &listing.items[filtered[selected]] : nullptr;
    }
    std::string Key(const Item &item) const { return item.path + "\n" + item.member; }
    void Select(int index)
    {
        const auto next = filtered.empty() ? -1 : std::clamp(index, 0, static_cast<int>(filtered.size()) - 1);
        if (selected != next && !preparingKey.empty())
        { ++prepareSerial; preparingKey.clear(); opening = false; calculating = false; }
        selected = next;
        selectedAt = SDL_GetTicks();
        if (const auto item = Selected()) filename = item->entry >= 0 ? item->member : item->path;
        else filename.clear();
    }
    void Close()
    {
        if (const auto item = Selected()) { lastPath = item->path; lastMember = item->member; }
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++prepareSerial; prepareRequested = false; preparedResult.reset();
            preparingKey.clear(); opening = false; calculating = false;
        }
        visible = false; Save();
    }
    void Prepare(const Item &item, bool forOpen, bool forCalculation = false)
    {
        std::lock_guard<std::mutex> lock(mutex);
        prepareRequestSerial = ++prepareSerial; preparedResult.reset(); prepareItem = item;
        prepareOpening = forOpen; prepareCalculating = forCalculation;
        prepareResources = resources; prepareContent = content; prepareRequested = true;
        preparingKey = Key(item); opening = forOpen; calculating = forCalculation; error.clear();
        condition.notify_one();
    }
    void Choose(const Item &item)
    {
        if (opening) return;
        if (item.directory || (Extension(item.path) == ".zip" && item.member.empty()))
        { Navigate(item.path); return; }
        Prepare(item, true);
    }
    void Open()
    {
        if (focus == 1)
        {
            try
            {
                const auto path = fs::absolute(fs::u8path(address)).lexically_normal();
                if (fs::is_directory(path) || Extension(path.u8string()) == ".zip") Navigate(path.u8string());
                else { Item item; item.path = path.u8string(); item.name = path.filename().u8string(); Choose(item); }
            }
            catch (const std::exception &e) { error = e.what(); }
        }
        else if (focus == 3 && !filename.empty())
        {
            if (const auto item = Selected(); item && filename == (item->entry >= 0 ? item->member : item->path)) { Choose(*item); return; }
            auto path = fs::u8path(filename);
            if (path.is_relative()) path = fs::u8path(location) / path;
            Item item; item.path = path.u8string(); item.name = path.filename().u8string();
            std::error_code ec; item.directory = fs::is_directory(path, ec); Choose(item);
        }
        else if (const auto item = Selected()) Choose(*item);
    }
    void Up()
    {
        if (recentView) { Navigate(location); return; }
        auto parent = fs::u8path(listing.location.empty() ? address : listing.location).parent_path();
        if (!parent.empty()) Navigate(parent.u8string());
    }
    Card *Preview(const Item &item)
    {
        auto found = cards.find(Key(item));
        if (found == cards.end()) return nullptr;
        found->second->used = SDL_GetTicks();
        return found->second.get();
    }
    void Tick()
    {
        std::unique_ptr<Prepared> ready;
        {
            std::lock_guard<std::mutex> lock(mutex);
            ready = std::move(preparedResult);
        }
        if (ready && ready->serial == prepareSerial)
        {
            preparingKey.clear(); opening = false; calculating = false;
            if (ready->calculating)
            {
                auto found = cards.find(ready->key);
                if (found != cards.end())
                {
                    const auto failure = ready->error.empty() ? ready->preview.error : ready->error;
                    found->second->calculation = failure.empty() ? ready->preview.details : "Duration unavailable\n" + failure;
                }
            }
            else if (ready->opening)
            {
                if (ready->error.empty())
                {
                    chosenPath = ready->path; chosenEntry = ready->entry;
                    chosen = ready->input.release(); Close(); return;
                }
                error = ready->error;
            }
            else
            {
                auto card = std::make_unique<Card>();
                try
                {
                    if (ready->error.empty()) card->preview = std::move(ready->preview);
                    else card->preview.error = ready->error;
                    auto &preview = card->preview;
                    if (!preview.rgba.empty())
                    {
                        auto surface = SDL_CreateSurfaceFrom(preview.width, preview.height, SDL_PIXELFORMAT_RGBA32,
                                                             preview.rgba.data(), preview.width * 4);
                        if (surface) { card->texture = SDL_CreateTextureFromSurface(renderer, surface); SDL_DestroySurface(surface); }
                        if (card->texture) SDL_SetTextureScaleMode(card->texture, SDL_SCALEMODE_NEAREST);
                        preview.rgba.clear();
                    }
                }
                catch (const std::exception &e) { card->preview.error = e.what(); }
                card->used = SDL_GetTicks();
                if (cards.size() >= 128)
                {
                    auto oldest = std::min_element(cards.begin(), cards.end(), [](const auto &a, const auto &b) { return a.second->used < b.second->used; });
                    if (oldest != cards.end()) cards.erase(oldest);
                }
                cards[ready->key] = std::move(card);
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (completed)
            {
                listing = std::move(*completed); completed.reset(); loading = false; error = listing.error;
                if (!recentView) location = listing.folder;
                Refilter();
                for (int i = 0; i < static_cast<int>(filtered.size()); ++i)
                {
                    const auto &item = listing.items[filtered[i]];
                    if (item.path == lastPath && item.member == lastMember) { Select(i); break; }
                }
            }
        }
        if (loading || !preparingKey.empty() || SDL_GetTicks() - selectedAt < 200 || SDL_GetTicks() - previewAt < 100) return;
        const Item *item = Selected();
        auto eligible = [&](const Item *candidate) {
            return candidate && !candidate->directory && !Preview(*candidate) &&
                   !(Extension(candidate->path) == ".zip" && candidate->member.empty());
        };
        if (!eligible(item))
        {
            item = nullptr;
            for (int i = visibleFirst; i < std::min(static_cast<int>(filtered.size()), visibleLast); ++i)
            {
                const auto &candidate = listing.items[filtered[i]];
                if (eligible(&candidate) && candidate.bytes <= 32 * 1024 * 1024)
                { item = &candidate; break; }
            }
        }
        if (!item) return;
        previewAt = SDL_GetTicks();
        Prepare(*item, false);
    }

};

extern "C"
{
MarcusBrowser *browser_create(SDL_Window *window, SDL_Renderer *renderer)
{
    try { return new MarcusBrowser(window, renderer); } catch (...) { return nullptr; }
}
void browser_destroy(MarcusBrowser *browser) { delete browser; }
void browser_show(MarcusBrowser *browser, const char *path, const char *resources, const char *content)
{
    if (!browser) return;
    try
    {
        browser->resources = resources ? resources : "";
        browser->content = content ? content : "";
        browser->error.clear(); browser->visible = true;
        std::string location = browser->location;
        if (path && *path)
        {
            const auto candidate = fs::absolute(fs::u8path(path));
            location = fs::is_directory(candidate) || Extension(candidate.u8string()) == ".zip" ?
                       candidate.u8string() : candidate.parent_path().u8string();
        }
        if (location.empty() || (!fs::is_directory(fs::u8path(location)) && Extension(location) != ".zip"))
            location = fs::current_path().u8string();
        browser->history.clear(); browser->Navigate(location, false);

    }
    catch (const std::exception &e) { browser->error = e.what(); }
}
int browser_visible(const MarcusBrowser *browser) { return browser && browser->visible; }

MarcusMovieInput *browser_take(MarcusBrowser *browser, int *entry, char *path, size_t capacity)
{
    if (!browser || !browser->chosen) return nullptr;
    auto chosen = browser->chosen; browser->chosen = nullptr;
    *entry = browser->chosenEntry; SDL_strlcpy(path, browser->chosenPath.c_str(), capacity);
    return chosen;
}
void browser_remember(MarcusBrowser *browser, const char *path, MarcusMovieInput *input, int entry)
{
    if (!browser || !path || !*path) return;
    try
    {
        if (!input) return;
        const auto member = input->Entry(entry);
        const auto absolute = fs::absolute(fs::u8path(path)).u8string();
        const std::string name = member.zipped ? member.name : "";
        auto &recent = browser->recent;
        recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const Recent &r) { return r.path == absolute && r.member == name; }), recent.end());
        recent.insert(recent.begin(), {absolute, name});
        if (recent.size() > 24) recent.resize(24);
        browser->Save();
    }
    catch (...) { /* A history failure must not prevent playback. */ }
}
}

namespace {
void MovieState(void *p, MarcusDialogState *out)
{
    auto &s = *static_cast<MarcusBrowser *>(p); *out = {};
    out->open = s.visible; out->kind = DIALOG_MOVIES; out->count = int(s.filtered.size()); out->selected = s.selected;
    out->grid = s.grid; out->filter = s.filter; out->sort_date = s.sortDate; out->recent = s.recentView;
    out->loading = s.loading; out->opening = s.opening; out->calculating = s.calculating;
    out->favourite_count = int(s.favourites.size()); out->can_back = !s.history.empty();
    out->is_favourite = std::find(s.favourites.begin(), s.favourites.end(), s.location) != s.favourites.end();
    SDL_strlcpy(out->title, "Open movie", sizeof(out->title)); SDL_strlcpy(out->confirm, "Open", sizeof(out->confirm));
    SDL_strlcpy(out->address, s.address.c_str(), sizeof(out->address)); SDL_strlcpy(out->search, s.search.c_str(), sizeof(out->search));
    SDL_strlcpy(out->filename, s.filename.c_str(), sizeof(out->filename)); SDL_strlcpy(out->error, s.error.c_str(), sizeof(out->error));
    SDL_snprintf(out->status, sizeof(out->status), "%d items%s", out->count, s.loading ? " / Reading folder..." : s.opening ? " / Preparing movie..." : s.calculating ? " / Calculating duration..." : "");
}
void MoviePrepare(void *p, SDL_Renderer *, int first, int last)
{
    auto &s = *static_cast<MarcusBrowser *>(p);
    try { if (first < 0) { if (s.visible) s.Tick(); } else { s.visibleFirst = first; s.visibleLast = last; } }
    catch (const std::exception &e) { s.error = e.what(); }
}
int MovieItem(void *p, SDL_Renderer *, int index, MarcusDialogItem *out)
{
    auto &s = *static_cast<MarcusBrowser *>(p); *out = {};
    try {
    if (index < 0) { const auto i = -index - 1; if (i >= int(s.favourites.size())) return 0;
        SDL_strlcpy(out->label, fs::u8path(s.favourites[i]).filename().u8string().c_str(), sizeof(out->label));
        SDL_strlcpy(out->detail, s.favourites[i].c_str(), sizeof(out->detail)); return 1; }
    if (index >= int(s.filtered.size())) return 0;
    const auto &item = s.listing.items[s.filtered[index]]; out->directory = item.directory;
    SDL_strlcpy(out->label, item.name.c_str(), sizeof(out->label));
    std::string detail = std::string(Kind(item)) + " / " + Size(item.bytes) + "\n" + item.path;
    if (const auto card = s.Preview(item)) {
        out->texture = card->texture; detail += "\n" + card->preview.title + "\n" + card->preview.details;
        if (!card->preview.error.empty()) detail += "\n" + card->preview.error;
        if (!card->calculation.empty()) detail += "\n" + card->calculation;
    }
    SDL_strlcpy(out->detail, detail.c_str(), sizeof(out->detail)); return 1;
    } catch (const std::exception &e) { s.error = e.what(); return 0; }
}
void MovieAction(void *p, int command, int index, float, const char *text)
{
    auto &s = *static_cast<MarcusBrowser *>(p);
    try { switch (command) {
    case DIALOG_SELECT: s.Select(index); break;
    case DIALOG_CANCEL: s.Close(); break;
    case DIALOG_ACCEPT: s.focus = 3; s.Open(); break;
    case DIALOG_ADDRESS: s.address = text ? text : ""; break;
    case DIALOG_FILENAME: s.filename = text ? text : ""; break;
    case DIALOG_SEARCH: s.search = text ? text : ""; s.Refilter(); break;
    case DIALOG_GO: s.focus = 1; s.Open(); break;
    case DIALOG_REFRESH: s.Navigate(s.recentView ? "" : s.location, false); break;
    case DIALOG_BACK: if (!s.history.empty()) { auto path = s.history.back(); s.history.pop_back(); s.Navigate(path, false); } break;
    case DIALOG_UP: s.Up(); break;
    case DIALOG_FILTER: s.filter = index; s.Refilter(); break;
    case DIALOG_SORT: s.sortDate = !s.sortDate; s.Refilter(); break;
    case DIALOG_GRID: s.grid = !s.grid; break;
    case DIALOG_RECENT: s.Navigate(""); break;
    case DIALOG_MOVIE_FOLDER: s.Navigate(s.movieFolder); break;
    case DIALOG_HOME: if (const auto home = SDL_GetUserFolder(SDL_FOLDER_HOME)) s.Navigate(home); break;
    case DIALOG_FAVOURITE: { auto found = std::find(s.favourites.begin(), s.favourites.end(), s.location);
        if (found == s.favourites.end()) { if (s.favourites.size() < 12) s.favourites.push_back(s.location); }
        else s.favourites.erase(found); s.Save(); break; }
    case DIALOG_FAVOURITE_GO: if (index >= 0 && index < int(s.favourites.size())) s.Navigate(s.favourites[index]); break;
    case DIALOG_CALCULATE:
        if (s.calculating) { ++s.prepareSerial; s.preparingKey.clear(); s.calculating = false; }
        else if (const auto item = s.Selected()) s.Prepare(*item, false, true);
        break;
    case DIALOG_DROP: { const auto path = fs::absolute(fs::u8path(text));
        if (fs::is_directory(path) || Extension(path.u8string()) == ".zip") s.Navigate(path.u8string());
        else { Item item; item.path = path.u8string(); item.name = path.filename().u8string(); s.Choose(item); } break; }
    } } catch (const std::exception &e) { s.error = e.what(); }
}
}
extern "C" const MarcusDialogHost marcus_movie_dialog_host = {MovieState, MoviePrepare, MovieItem, MovieAction};
