#ifndef MARCUS_ASM_PROTOCOL_H
#define MARCUS_ASM_PROTOCOL_H
#include "asm_wire.h"
#include "mm/preview.h"
#include <algorithm>
#include <cstdint>
namespace MarcusASM
{
// ASM6 also requires actor-capable live insertion. Older helpers use the same
// wire sizes but reject actor templates as if every added object were a prop.
using PropOffset = MMMovieActorPlacement;
using Request = MarcusASMRequest;
using Reply = MarcusASMReply;
inline void SetRevision(Request &request, std::uint64_t revision)
{
    request.edit_revision_low = static_cast<MMU32>(revision);
    request.edit_revision_high = static_cast<MMU32>(revision >> 32);
}
inline std::uint64_t Revision(const Request &request)
{
    return (std::uint64_t(request.edit_revision_high) << 32) | request.edit_revision_low;
}
inline Request Encode(int scene, int frame, const MM::PreviewView &v)
{
    Request r{};
    r.magic = MARCUS_ASM_MAGIC;
    r.last_frame = -1;
    r.scene = scene;
    r.frame = frame;
    r.width = v.width;
    r.height = v.height;
    r.yaw = v.yaw;
    r.pitch = v.pitch;
    r.zoom = v.zoom;
    r.roll = v.roll;
    r.fov = v.verticalFov;
    r.near_clip = v.nearClip;
    r.x = v.cameraX;
    r.y = v.cameraY;
    r.z = v.cameraZ;
    r.orbit_object = v.orbitObject;
    r.display_mode = v.displayMode;
    r.axis_view = v.axisView;
    r.flags = unsigned(v.authoredCamera) | unsigned(v.renderAtOutputResolution) << 1 | unsigned(v.freeCamera) << 2 |
              unsigned(v.frameSceneCamera) << 3 | unsigned(v.stableOrbit) << 4 | unsigned(v.includeDepth) << 5 |
              unsigned(v.fitSceneDepth) << 6 | unsigned(v.orthographic) << 7;
    return r;
}
inline MM::PreviewView Decode(const Request &r)
{
    MM::PreviewView v;
    v.width = r.width;
    v.height = r.height;
    v.yaw = r.yaw;
    v.pitch = r.pitch;
    v.zoom = r.zoom;
    v.roll = r.roll;
    v.verticalFov = r.fov;
    v.nearClip = r.near_clip;
    v.cameraX = r.x;
    v.cameraY = r.y;
    v.cameraZ = r.z;
    v.orbitObject = r.orbit_object;
    v.displayMode = r.display_mode;
    v.axisView = r.axis_view;
    v.authoredCamera = (r.flags & 1) != 0;
    v.renderAtOutputResolution = (r.flags & 2) != 0;
    v.freeCamera = (r.flags & 4) != 0;
    v.frameSceneCamera = (r.flags & 8) != 0;
    v.stableOrbit = (r.flags & 16) != 0;
    v.includeDepth = (r.flags & 32) != 0;
    v.fitSceneDepth = (r.flags & 64) != 0;
    v.orthographic = (r.flags & 128) != 0;
    return v;
}
inline MarcusASMCamera Encode(const MM::PreviewCamera &v)
{
    return {v.x, v.y, v.z, v.yaw, v.pitch, v.moveSpeed, v.roll, v.verticalFov};
}
inline MM::PreviewCamera Decode(const MarcusASMCamera &v)
{
    return {v.x, v.y, v.z, v.yaw, v.pitch, v.move_speed, v.roll, v.vertical_fov};
}
inline MarcusASMProjection Encode(const MM::PreviewProjection &v)
{
    MarcusASMProjection out{};
    out.valid = v.valid;
    std::copy_n(v.transform, 12, out.transform);
    out.vertical_fov = v.verticalFov;
    out.aspect = v.aspect;
    out.near_clip = v.nearClip;
    out.far_clip = v.farClip;
    out.display_depth = v.displayDepth;
    out.orthographic = v.orthographic;
    out.ortho_height = v.orthoHeight;
    return out;
}
inline MM::PreviewProjection Decode(const MarcusASMProjection &v)
{
    MM::PreviewProjection out;
    out.valid = v.valid != 0;
    std::copy_n(v.transform, 12, out.transform);
    out.verticalFov = v.vertical_fov;
    out.aspect = v.aspect;
    out.nearClip = v.near_clip;
    out.farClip = v.far_clip;
    out.displayDepth = v.display_depth;
    out.orthographic = v.orthographic != 0;
    out.orthoHeight = v.ortho_height;
    return out;
}
} // namespace MarcusASM
#endif
