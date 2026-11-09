#ifndef MARCUS_CONTENT_H
#define MARCUS_CONTENT_H
#include "three_mm/content_vfs.hpp"
#include <memory>
struct MarcusMovieFile;

class MarcusContent
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    ThreeMM::Content::Vfs files;
    std::filesystem::path movie;
    bool bundled = false;
    MarcusContent(const std::string &input, const std::string &baseDirectory);
    MarcusContent(std::shared_ptr<MarcusMovieFile> input, const std::string &baseDirectory);
    ~MarcusContent();
    void PrepareSource(const std::string &name);
};
#endif
