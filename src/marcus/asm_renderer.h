#ifndef MARCUS_ASM_RENDERER_H
#define MARCUS_ASM_RENDERER_H
#include "mm/preview.h"
#include <memory>
#include <string>
struct MarcusPlayer;
// Geometry only: captions, scrolling, audio and transitions stay in the host.
bool MarcusRenderLocalWorld(MarcusPlayer *, const MM::PreviewView &, MM::PreviewImage &, MM::Error &);
class MarcusASMRenderer
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    static bool Available(MM::Error &);
    MarcusASMRenderer(const std::string &movie, const std::string &resources, const std::string &content);
    ~MarcusASMRenderer();
    bool Render(int scene, int frame, const MM::PreviewView &, MM::PreviewImage &, MM::Error &);
};
#endif
