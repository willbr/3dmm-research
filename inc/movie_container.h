// Licensed under the MIT License.
#ifndef MOVIE_CONTAINER_H
#define MOVIE_CONTAINER_H
#pragma push_macro("size")
#undef size
#include <memory>
#pragma pop_macro("size")
namespace MovieContainer
{
// Host-owned container/session adapter. The engine keeps its ordinary chunky
// working file and wire layouts; packaging and content installation stay outside.
struct Document
{
    virtual ~Document() = default;
    virtual bool Bundled() const = 0;
    virtual bool Save(PFilename working, PFilename destination) = 0;
    virtual bool Commit() = 0; // True when source lists changed and browsers need rebuilding.
};
using Prepare = bool (*)(PFilename original, PFilename working, std::shared_ptr<Document> &document);
extern Prepare prepare;
inline bool IsVmm(PFilename path)
{
    return path && path->Ftg() == 'VMM';
}
} // namespace MovieContainer
#endif
