#include "movie_browser_worker.h"
#include "movie_input.h"
#include "preview_worker.h"
#include "mm/v3dmm.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
namespace
{
struct Item
{
    std::string name, path, member, basename;
    double bytes = 0;
    fs::file_time_type modified{};
    int entry = -1;
    bool directory = false;
};
struct ListingRequest
{
    std::string path;
    std::vector<std::pair<std::string, std::string>> recents;
    unsigned serial = 0;
};
struct MediaRequest
{
    std::string path, member, resources, content;
    int row, operation, entry;
    unsigned serial = 0;
};
}

struct MarcusBrowserListing
{
    std::string path, folder, error;
    std::vector<Item> items;
};
struct MarcusBrowserResult
{
    std::string path, error;
    int row, operation, entry = 0;
    BrowserPreview preview;
    std::unique_ptr<MarcusMovieInput> input;
};

namespace
{
std::unique_ptr<MarcusBrowserListing> ReadListing(const ListingRequest &job, const std::function<bool()> &cancelled)
{
    auto result = std::make_unique<MarcusBrowserListing>();
    result->path = job.path;
    try
    {
        const auto path = fs::u8path(job.path);
        if (job.path.empty())
        {
            for (const auto &recent : job.recents)
            {
                if (cancelled()) break;
                Item item;
                item.path = recent.first;
                item.member = recent.second;
                item.basename = fs::u8path(item.path).filename().u8string();
                item.name = item.member.empty() ? item.basename : item.member + "  |  " + item.basename;
                std::error_code ec;
                const auto bytes = fs::file_size(fs::u8path(item.path), ec);
                if (!ec) item.bytes = static_cast<double>(bytes);
                result->items.push_back(std::move(item));
            }
        }
        else if (marcus_browser_format(job.path.c_str()) == MARCUS_BROWSER_ZIP && !fs::is_directory(path))
        {
            result->folder = path.parent_path().u8string();
            const auto basename = path.filename().u8string();
            MarcusMovieInput zip;
            zip.Open(job.path, cancelled);
            for (int i = 0; i < zip.Count() && !cancelled(); ++i)
            {
                const auto member = zip.Entry(i);
                Item item;
                item.name = item.member = member.name;
                item.path = job.path;
                item.basename = basename;
                item.entry = i;
                item.bytes = member.bytes;
                result->items.push_back(std::move(item));
            }
        }
        else
        {
            result->folder = job.path;
            for (const auto &entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied))
            {
                if (cancelled()) break;
                std::error_code ec;
                Item item;
                item.directory = entry.is_directory(ec);
                item.name = item.basename = entry.path().filename().u8string();
                const auto format = marcus_browser_format(item.basename.c_str());
                if (!item.directory && format == MARCUS_BROWSER_OTHER) continue;
                item.path = entry.path().u8string();
                if (!item.directory)
                {
                    const auto bytes = entry.file_size(ec);
                    if (!ec) item.bytes = static_cast<double>(bytes);
                }
                item.modified = entry.last_write_time(ec);
                if (!item.directory && format == MARCUS_BROWSER_ZIP)
                {
                    // An archive with one movie can open directly from the folder listing.
                    try
                    {
                        MarcusMovieInput zip;
                        zip.Open(item.path, cancelled);
                        if (zip.Count() == 1)
                        {
                            const auto movie = zip.Entry(0);
                            item.member = movie.name;
                            item.entry = 0;
                            item.bytes = movie.bytes;
                            item.name = fs::u8path(item.member).filename().u8string();
                        }
                    }
                    catch (const std::exception &) { /* Keep the ZIP so opening can report its error. */ }
                }
                result->items.push_back(std::move(item));
                if (result->items.size() == 20000)
                {
                    result->error = "Showing the first 20,000 entries. Choose a smaller folder.";
                    break;
                }
            }
        }
    }
    catch (const std::exception &e) { result->error = e.what(); }
    if (!job.path.empty() && !result->error.empty())
    {
        const auto path = fs::u8path(job.path);
        std::error_code ec;
        result->folder = fs::is_directory(path, ec) ? job.path : path.parent_path().u8string();
    }
    return result;
}

std::shared_ptr<MarcusMovieFile> UnpackPreview(const MarcusMovieFile &file, const std::function<bool()> &cancelled)
{
    std::ifstream header(file.path, std::ios::binary);
    char signature[6] = {};
    header.read(signature, sizeof(signature));
    if (header.gcount() != 6 || std::string(signature, 6) != std::string("v3dmm\1", 6)) return {};

    // Stage the bundled movie bytes for the isolated reader. Pack installation
    // and live archive validation belong to the actual Open command.
    const auto range = MM::V3dmm::ReadBundle(file.path).movie;
    if (range.length > 512ull * 1024 * 1024)
        throw std::runtime_error("The bundled movie exceeds the preview size limit.");
    auto inner = std::make_shared<MarcusMovieFile>(fs::path{});
    for (int attempt = 0; attempt < 16 && inner->directory.empty(); ++attempt)
    {
        const auto directory = fs::temp_directory_path() / ("marcus-movie-" + std::to_string(std::random_device{}()));
        if (fs::create_directory(directory)) inner->directory = directory;
    }
    if (inner->directory.empty()) throw std::runtime_error("Cannot create a preview directory.");
    inner->path = inner->directory / "movie.3mm";
    std::ifstream source(range.file, std::ios::binary);
    std::ofstream output(inner->path, std::ios::binary);
    source.seekg(static_cast<std::streamoff>(range.offset));
    std::array<char, 65536> bytes;
    for (auto remaining = range.length; remaining;)
    {
        if (cancelled()) throw std::runtime_error("Movie preparation cancelled.");
        const auto count = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, bytes.size()));
        source.read(bytes.data(), count);
        output.write(bytes.data(), count);
        if (!source || !output) throw std::runtime_error("Cannot read the bundled movie preview.");
        remaining -= count;
    }
    output.close();
    if (!output) throw std::runtime_error("Cannot finish the bundled movie preview.");
    return inner;
}

std::unique_ptr<MarcusBrowserResult> PrepareMedia(const MediaRequest &job, const std::function<bool()> &cancelled)
{
    auto result = std::make_unique<MarcusBrowserResult>();
    result->path = job.path;
    result->row = job.row;
    result->operation = job.operation;
    try
    {
        auto input = std::make_unique<MarcusMovieInput>();
        input->Open(job.path, cancelled);
        int entry = std::max(0, job.entry);
        if (!job.member.empty())
        {
            for (entry = 0; entry < input->Count(); ++entry)
                if (job.member == input->Entry(entry).name) break;
            if (entry == input->Count()) throw std::runtime_error("This movie is no longer in the ZIP.");
        }
        result->entry = entry;
        const auto file = input->Materialize(entry, cancelled);
        if (job.operation == MARCUS_BROWSER_OPEN)
        {
            input->prepared = file;
            result->input = std::move(input);
        }
        else if (!cancelled())
        {
            const auto inner = job.operation == MARCUS_BROWSER_PREVIEW ? UnpackPreview(*file, cancelled) : nullptr;
            result->preview = RunPreviewWorker((inner ? inner->path : file->path).u8string(), job.resources,
                cancelled, job.content, job.operation == MARCUS_BROWSER_DURATION, file->fontDirectory.u8string());
        }
    }
    catch (const std::exception &e) { result->error = e.what(); }
    return result;
}
}

struct MarcusBrowserWorker
{
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false, listingFailed = false, mediaFailed = false;
    std::atomic<unsigned> latestListing{0}, latestMedia{0};
    std::unique_ptr<ListingRequest> listing;
    std::unique_ptr<MediaRequest> media;
    std::unique_ptr<MarcusBrowserListing> readyListing;
    std::unique_ptr<MarcusBrowserResult> readyMedia;
    std::thread thread;

    MarcusBrowserWorker() : thread([this] { Work(); }) {}
    ~MarcusBrowserWorker()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            ++latestListing;
            ++latestMedia;
        }
        wake.notify_one();
        thread.join();
    }
    void CancelListing()
    {
        ++latestListing;
        listing.reset();
        readyListing.reset();
        listingFailed = false;
    }
    void CancelMedia()
    {
        ++latestMedia;
        media.reset();
        readyMedia.reset();
        mediaFailed = false;
    }
    void Work()
    {
        for (;;)
        {
            std::unique_ptr<ListingRequest> scan;
            std::unique_ptr<MediaRequest> prepare;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return stopping || listing || media; });
                if (stopping) return;
                if (listing) scan = std::move(listing);
                else prepare = std::move(media);
            }
            if (scan)
            {
                const auto cancelled = [&] { return latestListing != scan->serial; };
                std::unique_ptr<MarcusBrowserListing> result;
                try { result = ReadListing(*scan, cancelled); } catch (...) {}
                std::lock_guard<std::mutex> lock(mutex);
                if (!cancelled())
                {
                    listingFailed = !result;
                    readyListing = std::move(result);
                }
            }
            else
            {
                const auto cancelled = [&] { return latestMedia != prepare->serial; };
                std::unique_ptr<MarcusBrowserResult> result;
                try { result = PrepareMedia(*prepare, cancelled); } catch (...) {}
                std::lock_guard<std::mutex> lock(mutex);
                if (!cancelled())
                {
                    mediaFailed = !result;
                    readyMedia = std::move(result);
                }
            }
        }
    }
};

extern "C"
{
MarcusBrowserWorker *marcus_browser_worker_create(void)
try { return new MarcusBrowserWorker; }
catch (...) { SDL_SetError("Cannot start the movie browser worker."); return nullptr; }

void marcus_browser_worker_destroy(MarcusBrowserWorker *worker) { delete worker; }

int marcus_browser_worker_list(MarcusBrowserWorker *worker, const char *path, const MarcusBrowserRecent *recent, int count)
try
{
    auto job = std::make_unique<ListingRequest>();
    job->path = path;
    if (!*path)
        for (int i = 0; i < count; ++i) job->recents.emplace_back(recent[i].path, recent[i].member);
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->CancelListing();
        worker->CancelMedia();
        job->serial = worker->latestListing;
        worker->listing = std::move(job);
    }
    worker->wake.notify_one();
    return 1;
}
catch (...) { SDL_SetError("Cannot request the movie listing."); return 0; }

int marcus_browser_worker_prepare(MarcusBrowserWorker *worker, const MarcusBrowserFile *file, int row, int operation,
                                  const char *resources, const char *content)
try
{
    auto job = std::make_unique<MediaRequest>();
    job->path = file->path;
    job->member = file->member;
    job->entry = file->entry;
    job->row = row;
    job->operation = operation;
    job->resources = resources;
    job->content = content;
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->CancelMedia();
        job->serial = worker->latestMedia;
        worker->media = std::move(job);
    }
    worker->wake.notify_one();
    return 1;
}
catch (...) { SDL_SetError("Cannot queue movie preparation."); return 0; }

void marcus_browser_worker_cancel(MarcusBrowserWorker *worker, int listing)
try
{
    std::lock_guard<std::mutex> lock(worker->mutex);
    if (listing) worker->CancelListing();
    worker->CancelMedia();
}
catch (...) {}

int marcus_browser_worker_take_listing(MarcusBrowserWorker *worker, MarcusBrowserListing **listing)
try
{
    std::lock_guard<std::mutex> lock(worker->mutex);
    *listing = worker->readyListing.release();
    if (!worker->listingFailed) return *listing ? 1 : 0;
    worker->listingFailed = false;
    SDL_SetError("Cannot read the movie listing.");
    return -1;
}
catch (...) { SDL_SetError("Cannot collect the movie listing."); return -1; }

int marcus_browser_worker_take_result(MarcusBrowserWorker *worker, MarcusBrowserResult **result)
try
{
    std::lock_guard<std::mutex> lock(worker->mutex);
    *result = worker->readyMedia.release();
    if (!worker->mediaFailed) return *result ? 1 : 0;
    worker->mediaFailed = false;
    SDL_SetError("Cannot prepare the movie.");
    return -1;
}
catch (...) { SDL_SetError("Cannot collect the movie preparation result."); return -1; }

void marcus_browser_listing_destroy(MarcusBrowserListing *listing) { delete listing; }
const char *marcus_browser_listing_path(const MarcusBrowserListing *listing) { return listing->path.c_str(); }
const char *marcus_browser_listing_folder(const MarcusBrowserListing *listing) { return listing->folder.c_str(); }
const char *marcus_browser_listing_error(const MarcusBrowserListing *listing) { return listing->error.c_str(); }
int marcus_browser_listing_count(const MarcusBrowserListing *listing) { return static_cast<int>(listing->items.size()); }

void marcus_browser_listing_file(const MarcusBrowserListing *listing, int index, MarcusBrowserFile *row)
{
    const auto &item = listing->items[index];
    const auto stamp = static_cast<std::uint64_t>(item.modified.time_since_epoch().count()) ^ (1ull << 63);
    *row = {item.name.c_str(), item.path.c_str(), item.member.c_str(), item.basename.c_str(), item.bytes,
            static_cast<unsigned long>(stamp >> 32), static_cast<unsigned long>(stamp & 0xffffffff),
            item.entry, item.directory};
}

void marcus_browser_result_destroy(MarcusBrowserResult *result) { delete result; }
void marcus_browser_result_media(const MarcusBrowserResult *result, MarcusBrowserMedia *media)
{
    const auto &preview = result->preview;
    const auto &error = result->error.empty() ? preview.error : result->error;
    *media = {result->path.c_str(), preview.title.c_str(), preview.details.c_str(), error.c_str(),
              preview.rgba.empty() ? nullptr : preview.rgba.data(), result->operation, result->row,
              result->entry, preview.width, preview.height};
}
MarcusMovieInput *marcus_browser_result_take_input(MarcusBrowserResult *result) { return result->input.release(); }

char *marcus_browser_path_absolute(const char *path, int normalize)
try
{
    auto absolute = fs::absolute(fs::u8path(path));
    if (normalize) absolute = absolute.lexically_normal();
    return SDL_strdup(absolute.u8string().c_str());
}
catch (const std::exception &e) { SDL_SetError("%s", e.what()); return nullptr; }

char *marcus_browser_path_parent(const char *path)
try { return SDL_strdup(fs::u8path(path).parent_path().u8string().c_str()); }
catch (const std::exception &e) { SDL_SetError("%s", e.what()); return nullptr; }

char *marcus_browser_path_basename(const char *path)
try { return SDL_strdup(fs::u8path(path).filename().u8string().c_str()); }
catch (const std::exception &e) { SDL_SetError("%s", e.what()); return nullptr; }

char *marcus_browser_path_join(const char *directory, const char *path)
try { return SDL_strdup((fs::u8path(directory) / fs::u8path(path)).u8string().c_str()); }
catch (const std::exception &e) { SDL_SetError("%s", e.what()); return nullptr; }

char *marcus_browser_current_directory(void)
try { return SDL_strdup(fs::current_path().u8string().c_str()); }
catch (const std::exception &e) { SDL_SetError("%s", e.what()); return nullptr; }

char *marcus_browser_movie_folder(void)
try
{
    if (const auto documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS))
    {
        const auto directory = fs::u8path(documents) / "3DMMForever";
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (!ec && fs::is_directory(directory, ec)) return SDL_strdup(directory.u8string().c_str());
    }
    return marcus_browser_current_directory();
}
catch (const std::exception &e) { SDL_SetError("%s", e.what()); return nullptr; }
}
