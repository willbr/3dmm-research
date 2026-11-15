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
bool MarcusApplyLiveEdits(MarcusPlayer *, const std::vector<MM::PreviewSceneDefinition> &,
                         const std::vector<MM::PreviewPropDefinition> &, const std::vector<MarcusASM::CameraEdit> &,
                         std::uint64_t revision, MM::Error &);
bool MarcusApplySceneRange(MarcusPlayer *, std::int32_t first, std::int32_t last, MM::Error &);
bool MarcusApplyPropOffsets(MarcusPlayer *, const std::vector<MarcusASM::PropOffset> &, MM::Error &);
class MarcusASMRenderer
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    static bool Available(MM::Error &);
    MarcusASMRenderer(const std::string &movie, const std::string &resources, const std::string &content);
    ~MarcusASMRenderer();
    bool Render(int scene, int frame, const MM::PreviewView &, MM::PreviewImage &, MM::Error &,
                const std::vector<MarcusASM::PropOffset> &offsets = {}, std::int32_t firstFrame = 0, std::int32_t lastFrame = -1, const MarcusLiveEdits *edits = nullptr);
};
#endif
