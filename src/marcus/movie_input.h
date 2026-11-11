#ifndef MARCUS_MOVIE_INPUT_H
#define MARCUS_MOVIE_INPUT_H
#include "player.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <functional>

// A selected ZIP member has a private, flat staging path. A shared lease keeps
// it alive through renderer/content teardown and resource reloads.
struct MarcusMovieFile
{
    std::filesystem::path path, directory;
    explicit MarcusMovieFile(std::filesystem::path path) : path(std::move(path))
    {
    }
    ~MarcusMovieFile();
};
struct MarcusMovieInput
{
    struct Impl;
    std::unique_ptr<Impl> impl;
    char error[1024] = {};
    // A background loader can retain its verified extraction until playback
    // takes a lease, avoiding a second decompression on the UI thread.
    std::shared_ptr<MarcusMovieFile> prepared;
    MarcusMovieInput();
    ~MarcusMovieInput();
    void Open(const std::string &path, const std::function<bool()> &cancelled = {});
    int Count() const;
    MarcusMovieEntry Entry(int index) const;
    std::shared_ptr<MarcusMovieFile> Materialize(int index, const std::function<bool()> &cancelled = {});
};
#endif
