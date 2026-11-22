#ifndef MARCUS_ASM_RENDERER_H
#define MARCUS_ASM_RENDERER_H
#include "asm_protocol.h"
#include <memory>
#include <string>
struct MarcusPlayer;
struct MarcusLiveEdits;
// Geometry only: captions, scrolling, audio and transitions stay in the host.
bool MarcusRenderLocalWorld(MarcusPlayer *, const MM::PreviewView &, MM::PreviewImage &, MM::Error &);
bool MarcusPrepareEmpty(MarcusPlayer *, const char *resources, const char *content, MM::Error &);
bool MarcusApplyLiveEdits(MarcusPlayer *, const MarcusASMRequest &, const MMPreviewLiveScene *,
                         const MMPreviewLiveProp *, const MMMovieCameraEdit *, MM::Error &);
bool MarcusApplySceneRange(MarcusPlayer *, std::int32_t first, std::int32_t last, MM::Error &);
bool MarcusApplyPropOffsets(MarcusPlayer *, const MarcusASM::PropOffset *, std::size_t count, MM::Error &);
class MarcusASMRenderer
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    static bool Available(MM::Error &);
    MarcusASMRenderer(const std::string &movie, const std::string &resources, const std::string &content, const std::string &origin = {});
    ~MarcusASMRenderer();
    bool Render(int scene, int frame, const MM::PreviewView &, MM::PreviewImage &, MM::Error &,
                const MarcusASM::PropOffset *, std::uint32_t count, std::int32_t firstFrame, std::int32_t lastFrame,
                const MarcusLiveEdits *);
};
#endif
