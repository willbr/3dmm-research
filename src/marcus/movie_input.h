#ifndef MARCUS_MOVIE_INPUT_H
#define MARCUS_MOVIE_INPUT_H
#include "player.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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
    MarcusMovieInput();
    ~MarcusMovieInput();
    void Open(const std::string &path);
    int Count() const;
    MarcusMovieEntry Entry(int index) const;
    std::shared_ptr<MarcusMovieFile> Materialize(int index);
};
#endif
