#include "movie_browser.h"
#include "movie_input.h"
#include "preview_worker.h"
#include "three_mm/v3dmm.h"
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
constexpr unsigned long panel = 0x25282d, text = 0xdde0e5, muted = 0x969da8, accent = 0x78cbc4;
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
    bool opening = false;
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
    ~Card() { SDL_DestroyTexture(texture); }
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
    return item.directory ? "FOLDER" : extension == ".zip" ? "ZIP" : extension == ".vmm" ? "VMM" : "3MM";
}
std::string Size(double bytes)
{
    char value[64];
    SDL_snprintf(value, sizeof(value), bytes >= 1048576 ? "%.1f MiB" : "%.0f KiB",
                 bytes / (bytes >= 1048576 ? 1048576 : 1024));
    return value;
}
void Backspace(std::string &value)
{
    if (value.empty()) return;
    auto at = value.size() - 1;
    while (at && (static_cast<unsigned char>(value[at]) & 0xc0) == 0x80) --at;
    value.resize(at);
}
}

struct MarcusBrowser
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    bool visible = false, recentView = false, grid = true, sortDate = false, loading = false, replaceText = false;
    int focus = 0, selected = -1, scroll = 0, filter = 0;
    float x = 0, y = 0, w = 1000, h = 650, listW = 510;
    int rows = 3;
    bool scrollbarDragging = false;
    float scrollbarOffset = 0;
    std::string location, movieFolder, address, search, filename, error, resources, statePath, lastPath, lastMember;
    std::vector<std::string> history, favourites;
    std::vector<Recent> recent;
    std::vector<int> filtered;
    Listing listing;
    std::map<std::string, std::unique_ptr<Card>> cards;
    Uint64 selectedAt = 0, previewAt = 0, scrollAt = 0;
    std::string clicked;
    Uint64 clickedAt = 0;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable condition;
    bool stopping = false, requested = false, prepareRequested = false, opening = false;
    Item prepareItem;
    bool prepareOpening = false;
    std::string preparingKey, prepareResources;
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
            bool preparing = false, forOpen = false;
            std::string resourcePath;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this] { return requested || prepareRequested || stopping; });
                if (stopping) return;
                id = generation;
                if (requested) { path = requestedPath; recents = requestedRecent; requested = false; }
                else
                {
                    preparing = true; item = prepareItem; forOpen = prepareOpening;
                    serial = prepareRequestSerial; resourcePath = prepareResources; prepareRequested = false;
                }
            }
            if (preparing)
            {
                auto ready = std::make_unique<Prepared>();
                ready->serial = serial; ready->key = Key(item); ready->path = item.path; ready->opening = forOpen;
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
                    if (!forOpen && !cancelled())
                    {
                        std::ifstream header(ready->file->path, std::ios::binary);
                        char signature[6] = {};
                        header.read(signature, sizeof(signature));
                        if (header.gcount() == 6 && std::string(signature, 6) == std::string("v3dmm\1", 6))
                        {
                            // Read only the inner movie. Previewing must not install packs
                            // or invoke engine archive validation on this worker.
                            const auto range = ThreeMM::V3dmm::ReadBundle(ready->file->path).movie;
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
                    if (!forOpen && !cancelled()) ready->preview = RunPreviewWorker(ready->movie, resourcePath, cancelled);
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
        selected = -1; scroll = 0; cards.clear(); filtered.clear(); listing = {};
        loading = true; focus = 0; replaceText = false; scrollbarDragging = false;
        if (Extension(path) == ".zip" && filter == 3) filter = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            requestedPath = path; requestedRecent = recent;
            ++generation; latest = generation; completed.reset(); requested = true;
            ++prepareSerial; prepareRequested = false; preparedResult.reset();
            preparingKey.clear(); opening = false;
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
            const auto ext = Extension(item.member.empty() ? item.path : item.member);
            if (!item.directory && filter && ext != (filter == 1 ? ".3mm" : filter == 2 ? ".vmm" : ".zip")) continue;
            if (Lower(item.name).find(query) != std::string::npos) filtered.push_back(i);
        }
        if (!recentView)
            std::stable_sort(filtered.begin(), filtered.end(), [this](int a, int b) {
                const auto &left = listing.items[a], &right = listing.items[b];
                if (left.directory != right.directory) return left.directory;
                if (sortDate && left.modified != right.modified) return left.modified > right.modified;
                return Lower(left.name) < Lower(right.name);
            });
        Select(filtered.empty() ? -1 : 0); scroll = 0;
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
        { ++prepareSerial; preparingKey.clear(); opening = false; }
        selected = next;
        selectedAt = SDL_GetTicks();
        if (const auto item = Selected()) filename = item->entry >= 0 ? item->member : item->path;
        else filename.clear();
        const int columns = grid ? 3 : 1;
        if (selected >= 0)
        {
            const int row = selected / columns;
            if (row < scroll) scroll = row;
            if (row >= scroll + rows) scroll = row - rows + 1;
        }
    }
    void Close()
    {
        if (const auto item = Selected()) { lastPath = item->path; lastMember = item->member; }
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++prepareSerial; prepareRequested = false; preparedResult.reset();
            preparingKey.clear(); opening = false;
        }
        visible = false; scrollbarDragging = false; SDL_StopTextInput(window); Save();
    }
    void Prepare(const Item &item, bool forOpen)
    {
        std::lock_guard<std::mutex> lock(mutex);
        prepareRequestSerial = ++prepareSerial; preparedResult.reset(); prepareItem = item;
        prepareOpening = forOpen; prepareResources = resources; prepareRequested = true;
        preparingKey = Key(item); opening = forOpen; error.clear();
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
            preparingKey.clear(); opening = false;
            if (ready->opening)
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
        if (loading || scrollbarDragging || SDL_GetTicks() - scrollAt < 250 || !preparingKey.empty() || SDL_GetTicks() - selectedAt < 200 || SDL_GetTicks() - previewAt < 100) return;
        const Item *item = Selected();
        auto eligible = [&](const Item *candidate) {
            return candidate && !candidate->directory && !Preview(*candidate) &&
                   !(Extension(candidate->path) == ".zip" && candidate->member.empty());
        };
        if (!eligible(item))
        {
            item = nullptr;
            const int columns = grid ? 3 : 1;
            for (int i = scroll * columns; i < std::min(static_cast<int>(filtered.size()), (scroll + rows) * columns); ++i)
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

    void Image(MarcusUI *ui, const Item &item, SDL_FRect box)
    {
        ui_fill(ui, box.x, box.y, box.w, box.h, 0x17191c);
        const auto card = Preview(item);
        if (card && card->texture)
        {
            const auto scale = std::min(box.w / card->preview.width, box.h / card->preview.height);
            SDL_FRect target{box.x + (box.w - card->preview.width * scale) / 2,
                             box.y + (box.h - card->preview.height * scale) / 2,
                             card->preview.width * scale, card->preview.height * scale};
            SDL_RenderTexture(renderer, card->texture, nullptr, &target);
        }
        else
        {
            ui_text(ui, box.x + 12, box.y + box.h / 2 - 8, item.directory ? "[ Folder ]" : Kind(item), accent);
        }
        // Film perforations recall the retail movie browser without borrowed art.
        if (!item.directory)
            for (float at = box.x + 5; at < box.x + box.w - 5; at += 16)
            {
                ui_fill(ui, at, box.y + 3, 7, 4, 0x555b63);
                ui_fill(ui, at, box.y + box.h - 7, 7, 4, 0x555b63);
            }
    }
    int MaximumScroll() const
    {
        const int columns = grid ? 3 : 1;
        return std::max(0, (static_cast<int>(filtered.size()) + columns - 1) / columns - rows);
    }
    SDL_FRect ScrollTrack() const { return {x + 180 + listW - 14, y + 132, 12, rows * (grid ? 120.0f : 32.0f) - 6}; }
    SDL_FRect ScrollThumb() const
    {
        auto track = ScrollTrack();
        const int maximum = MaximumScroll();
        const float size = maximum ? std::max(28.0f, track.h * rows / (maximum + rows)) : track.h;
        track.y += maximum ? (track.h - size) * scroll / maximum : 0;
        track.h = size;
        return track;
    }
    void ScrollActivity()
    {
        scrollAt = SDL_GetTicks();
        if (!opening && !preparingKey.empty())
        { ++prepareSerial; preparingKey.clear(); }
    }
    void DragScrollbar(float mouseY)
    {
        ScrollActivity();
        const auto track = ScrollTrack(), thumb = ScrollThumb();
        const auto travel = track.h - thumb.h;
        if (travel > 0)
            scroll = std::clamp(static_cast<int>((mouseY - scrollbarOffset - track.y) * MaximumScroll() / travel + 0.5f), 0, MaximumScroll());
    }
    void Draw(MarcusUI *ui, int width, int height)
    {
        Tick();
        if (!visible) return;
        w = std::min(1180.0f, width - 40.0f); h = std::min(730.0f, height - 60.0f);
        x = (width - w) / 2; y = (height - h) / 2; listW = w - 492;
        const float listX = x + 180, top = y + 132, bottom = y + h - 116;
        rows = std::max(1, static_cast<int>((bottom - top) / (grid ? 120 : 32)));
        const int columns = grid ? 3 : 1;
        const int maximum = MaximumScroll();
        scroll = std::clamp(scroll, 0, maximum);
        ui_opacity(ui, 0.75f); ui_fill(ui, 0, 0, static_cast<float>(width), static_cast<float>(height), 0x090a0c); ui_opacity(ui, 1);
        ui_fill(ui, x - 1, y - 1, w + 2, h + 2, 0x606670);
        ui_fill(ui, x, y, w, h, panel);
        ui_text(ui, x + 20, y + 16, "OPEN A MOVIE", text);
        ui_text(ui, x + 180, y + 18, "Movies / ZIP collections", muted);
        ui_button(ui, x + 16, y + 50, 68, "Back", 0, !history.empty());
        ui_button(ui, x + 92, y + 50, 68, "Up", 0, 1);
        auto field = [&](float left, float topY, float size, const std::string &value, int id) {
            ui_fill(ui, left, topY, size, 28, focus == id ? 0x34464c : 0x191c20);
            ui_label(ui, left + 8, topY + 6, size - 16, value.c_str(), focus == id ? accent : text);
            if (focus == id)
                ui_fill(ui, left + 5, topY + 4, 2, 20, accent);
        };
        field(x + 180, y + 50, w - 300, recentView && focus != 1 ? "Recent movies" : address, 1);
        ui_button(ui, x + w - 108, y + 50, 88, "Go", 0, 1);
        ui_button(ui, x + 16, y + 100, 144, "Recent movies", recentView, 1);
        ui_button(ui, x + 16, y + 138, 144, "Movie folder", !recentView && listing.location == movieFolder, 1);
        ui_button(ui, x + 16, y + 176, 144, "Home", 0, 1);
        ui_text(ui, x + 20, y + 226, "FAVOURITES", muted);
        ui_button(ui, x + 16, y + 250, 144,
                  std::find(favourites.begin(), favourites.end(), location) == favourites.end() ? "+ This folder" : "- This folder", 0, !location.empty());
        for (std::size_t i = 0; i < favourites.size() && y + 294 + i * 32 < bottom; ++i)
            ui_label(ui, x + 22, y + 296 + static_cast<float>(i) * 32, 132, fs::u8path(favourites[i]).filename().u8string().c_str(), text);
        field(listX, y + 94, listW - 212, search.empty() && focus != 2 ? "Search movies..." : search, 2);
        ui_button(ui, listX + listW - 204, y + 94, 96, sortDate && !listing.input ? "Newest" : "Name A-Z",
                  sortDate && !listing.input, !recentView && !listing.input);
        ui_button(ui, listX + listW - 100, y + 94, 100, grid ? "Cards" : "List", grid, 1);
        for (int row = 0; row < rows; ++row)
            for (int col = 0; col < columns; ++col)
            {
                const int index = (row + scroll) * columns + col;
                if (index >= static_cast<int>(filtered.size())) continue;
                const auto &item = listing.items[filtered[index]];
                const float cellW = (listW - 22) / columns, cellH = grid ? 120.0f : 32.0f;
                const float left = listX + col * cellW, cellY = top + row * cellH;
                ui_fill(ui, left, cellY, cellW - 6, cellH - 6, index == selected ? 0x344c50 : 0x2e3238);
                if (grid)
                {
                    Image(ui, item, {left + 6, cellY + 6, cellW - 18, 72});
                    ui_label(ui, left + 8, cellY + 83, cellW - 22, item.name.c_str(), index == selected ? accent : text);
                    ui_text(ui, left + 8, cellY + 102, Kind(item), muted);
                }
                else
                {
                    ui_text(ui, left + 8, cellY + 6, Kind(item), accent);
                    ui_label(ui, left + 70, cellY + 6, cellW - 150, item.name.c_str(), text);
                    ui_label(ui, left + cellW - 82, cellY + 6, 70, item.directory ? "" : Size(item.bytes).c_str(), muted);
                }
            }
        const auto track = ScrollTrack(), thumb = ScrollThumb();
        ui_fill(ui, track.x, track.y, track.w, track.h, 0x191c20);
        ui_fill(ui, thumb.x + 1, thumb.y, thumb.w - 2, thumb.h,
                !maximum ? 0x363a40 : scrollbarDragging ? accent : 0x64717a);
        if (loading || filtered.empty())
            ui_label(ui, listX + 12, top + 36, listW - 24, loading ? "Finding movies..." : "No matching movies in this location.", muted);
        ui_text(ui, listX, bottom + 8, (std::to_string(filtered.size()) + " items / Scroll or Page Up / Down").c_str(), muted);
        const float detailX = x + w - 292;
        ui_line(ui, detailX - 14, y + 94, detailX - 14, bottom + 28, 0x41464e);
        ui_text(ui, detailX, y + 100, "MOVIE PREVIEW", muted);
        if (const auto item = Selected())
        {
            Image(ui, *item, {detailX, top, 270, 166});
            ui_label(ui, detailX, top + 184, 270, item->name.c_str(), text);
            ui_text(ui, detailX, top + 212, item->directory ? "Folder" : (std::string(Kind(*item)) + " / " + Size(item->bytes)).c_str(), accent);
            ui_label(ui, detailX, top + 240, 270, item->path.c_str(), muted);
            if (const auto card = Preview(*item))
            {
                ui_label(ui, detailX, top + 270, 270, card->preview.title.c_str(), text);
                ui_label(ui, detailX, top + 296, 270, card->preview.details.c_str(), muted);
                ui_label(ui, detailX, top + 326, 270, card->preview.error.empty() ?
                         (card->texture ? "Saved scene thumbnail" : "No saved preview available") : card->preview.error.c_str(), muted);
            }
            else ui_label(ui, detailX, top + 270, 270, item->directory ? "Open to browse this folder." :
                          Extension(item->path) == ".zip" && item->member.empty() ? "Open movie or browse compilation." : "Reading saved preview...", muted);
            ui_label(ui, detailX, bottom - 2, 270, "Dependencies checked when opened.", muted);
        }
        ui_text(ui, x + 20, y + h - 76, "File", muted);
        field(listX, y + h - 84, listW, filename, 3);
        static const char *filters[] = {"All movies + ZIPs", "3MM movies", "VMM movies", "ZIP archives"};
        ui_button(ui, detailX, y + h - 84, 270, filters[filter], 0, 1);
        ui_label(ui, x + 20, y + h - 38, w - 260, opening ? "Preparing movie... / Esc to cancel" : error.empty() ? "Double-click to open / Enter to confirm / Esc to cancel" : error.c_str(), error.empty() ? muted : 0xf0ae8b);
        ui_button(ui, x + w - 228, y + h - 44, 96, "Cancel", 0, 1);
        ui_button(ui, x + w - 116, y + h - 44, 96, "Open", 1, !loading && !opening && (!filename.empty() || Selected()));
    }
    void Click(float px, float py)
    {
        const auto track = ScrollTrack(), thumb = ScrollThumb();
        if (ui_inside(px, py, track.x, track.y, track.w, track.h))
        {
            if (!MaximumScroll()) return;
            ScrollActivity();
            if (ui_inside(px, py, thumb.x, thumb.y, thumb.w, thumb.h))
            { scrollbarDragging = true; scrollbarOffset = py - thumb.y; }
            else scroll = std::clamp(scroll + (py < thumb.y ? -rows : rows), 0, MaximumScroll());
            return;
        }
        auto hit = [&](float left, float top, float width, float height = 28) { return ui_inside(px, py, x + left, y + top, width, height) != 0; };
        if (hit(16, 50, 68)) { if (!history.empty()) { auto path = history.back(); history.pop_back(); Navigate(path, false); } }
        else if (hit(92, 50, 68)) Up();
        else if (hit(180, 50, w - 300)) { focus = 1; replaceText = true; }
        else if (hit(w - 108, 50, 88)) { focus = 1; Open(); }
        else if (hit(16, 100, 144)) Navigate("");
        else if (hit(16, 138, 144)) Navigate(movieFolder);
        else if (hit(16, 176, 144)) { if (auto home = SDL_GetUserFolder(SDL_FOLDER_HOME)) Navigate(home); }
        else if (hit(16, 250, 144) && !location.empty())
        {
            auto at = std::find(favourites.begin(), favourites.end(), location);
            if (at != favourites.end()) favourites.erase(at);
            else if (favourites.size() < 12) favourites.push_back(location);
            Save();
        }
        else if (hit(16, 290, 144, h - 410))
        { const auto i = static_cast<std::size_t>((py - y - 290) / 32); if (i < favourites.size()) Navigate(favourites[i]); }
        else if (hit(180, 94, listW - 212)) { focus = 2; replaceText = true; }
        else if (hit(180 + listW - 204, 94, 96) && !recentView && !listing.input) { sortDate = !sortDate; Refilter(); }
        else if (hit(180 + listW - 100, 94, 100)) { grid = !grid; scroll = 0; }
        else if (hit(180, h - 84, listW)) { focus = 3; replaceText = true; }
        else if (hit(w - 292, h - 84, 270)) { filter = (filter + 1) % 4; Refilter(); }
        else if (hit(w - 228, h - 44, 96)) Close();
        else if (hit(w - 116, h - 44, 96)) Open();
        else if (hit(180, 132, listW - 22, static_cast<float>(rows) * (grid ? 120 : 32)))
        {
            focus = 0;
            const int columns = grid ? 3 : 1;
            const int index = (static_cast<int>((py - y - 132) / (grid ? 120 : 32)) + scroll) * columns +
                              (grid ? static_cast<int>((px - x - 180) / ((listW - 22) / 3)) : 0);
            if (index >= static_cast<int>(filtered.size())) return;
            Select(index);
            const auto item = Selected();
            const auto key = Key(*item);
            if (clicked == key && SDL_GetTicks() - clickedAt < 450) { clicked.clear(); Choose(*item); }
            else { clicked = key; clickedAt = SDL_GetTicks(); }
        }
        else focus = 0;
    }
    void KeyDown(SDL_Keycode key, SDL_Keymod mod)
    {
        if (key == SDLK_ESCAPE) { Close(); return; }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) { Open(); return; }
        if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && key == SDLK_L) { focus = 1; replaceText = true; return; }
        if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && key == SDLK_F) { focus = 2; replaceText = true; return; }
        if ((mod & SDL_KMOD_ALT) && key == SDLK_UP) { Up(); return; }
        if ((mod & SDL_KMOD_ALT) && key == SDLK_LEFT)
        { if (!history.empty()) { auto path = history.back(); history.pop_back(); Navigate(path, false); } return; }
        if (key == SDLK_TAB) { focus = (focus + ((mod & SDL_KMOD_SHIFT) ? 3 : 1)) % 4; replaceText = true; return; }
        if (key == SDLK_F5) { Navigate(recentView ? "" : listing.location, false); return; }
        if (focus)
        {
            auto &value = focus == 1 ? address : focus == 2 ? search : filename;
            if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && key == SDLK_A) { replaceText = true; return; }
            else if (key == SDLK_BACKSPACE)
            { if (replaceText) value.clear(); else Backspace(value); replaceText = false; }
            else if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && key == SDLK_V)
            { if (auto clipboard = SDL_GetClipboardText()) { if (replaceText) value.clear(); replaceText = false; value.append(clipboard); SDL_free(clipboard); } }
            if (value.size() > 4096) value.resize(4096);
            if (focus == 2) Refilter();
            return;
        }
        const int columns = grid ? 3 : 1;
        if (key == SDLK_UP) Select(selected - columns);
        else if (key == SDLK_DOWN) Select(selected + columns);
        else if (key == SDLK_LEFT) Select(selected - 1);
        else if (key == SDLK_RIGHT) Select(selected + 1);
        else if (key == SDLK_PAGEUP) Select(selected - rows * columns);
        else if (key == SDLK_PAGEDOWN) Select(selected + rows * columns);
        else if (key == SDLK_HOME) Select(0);
        else if (key == SDLK_END) Select(static_cast<int>(filtered.size()) - 1);
        else if (key == SDLK_BACKSPACE) Up();
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
        (void)content;
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
        SDL_StartTextInput(browser->window);
    }
    catch (const std::exception &e) { browser->error = e.what(); }
}
int browser_visible(const MarcusBrowser *browser) { return browser && browser->visible; }
int browser_event(MarcusBrowser *browser, const SDL_Event *event)
{
    if (!browser_visible(browser)) return 0;
    try
    {
        if (event->type == SDL_EVENT_KEY_DOWN) browser->KeyDown(event->key.key, event->key.mod);
        else if (event->type == SDL_EVENT_TEXT_INPUT)
        {
            if (!browser->focus) browser->focus = 2;
            auto &value = browser->focus == 1 ? browser->address : browser->focus == 2 ? browser->search : browser->filename;
            if (browser->replaceText) { value.clear(); browser->replaceText = false; }
            if (value.size() + SDL_strlen(event->text.text) <= 4096) value += event->text.text;
            if (browser->focus == 2) browser->Refilter();
        }
        else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
            browser->Click(event->button.x, event->button.y);
        else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT)
            browser->scrollbarDragging = false;
        else if (event->type == SDL_EVENT_MOUSE_MOTION && browser->scrollbarDragging)
            browser->DragScrollbar(event->motion.y);
        else if (event->type == SDL_EVENT_MOUSE_WHEEL)
        {
            browser->ScrollActivity();
            browser->scroll = std::max(0, browser->scroll - static_cast<int>(event->wheel.y *
                (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1) * (browser->grid ? 1 : 3)));
        }
        else if (event->type == SDL_EVENT_DROP_FILE)
        {
            const auto path = fs::absolute(fs::u8path(event->drop.data));
            if (fs::is_directory(path) || Extension(path.u8string()) == ".zip") browser->Navigate(path.u8string());
            else { Item item; item.path = path.u8string(); item.name = path.filename().u8string(); browser->Choose(item); }
        }
    }
    catch (const std::exception &e) { browser->error = e.what(); }
    return event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP || event->type == SDL_EVENT_TEXT_INPUT ||
           event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
           event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_WHEEL || event->type == SDL_EVENT_DROP_FILE;
}
void browser_draw(MarcusBrowser *browser, MarcusUI *ui, int width, int height)
{
    if (browser_visible(browser))
        try { browser->Draw(ui, width, height); }
        catch (const std::exception &e) { browser->error = e.what(); }
}
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
