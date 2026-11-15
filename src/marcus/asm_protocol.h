#ifndef MARCUS_ASM_PROTOCOL_H
#define MARCUS_ASM_PROTOCOL_H
#include "mm/preview.h"
#include <cstdint>
namespace MarcusASM
{
constexpr std::uint32_t Magic = 0x324d5341; // ASM2, little endian x86/x64 only.
struct PropOffset
{
    std::int32_t object = 0;
    float offset[3] = {};
};
struct Request
{
    std::uint32_t magic = Magic;
    std::int32_t scene = 0, frame = 0;
    std::uint32_t width = 0, height = 0, flags = 0;
    float yaw = 0, pitch = 0, zoom = 1, roll = 0, fov = 45, nearClip = 0;
    float x = 0, y = 0, z = 0;
    std::int32_t orbitObject = -1, displayMode = 0, axisView = 0;
    std::uint32_t propCount = 0;
};
inline Request Encode(int scene, int frame, const MM::PreviewView &v)
{
    Request r;
    r.scene = scene;
    r.frame = frame;
    r.width = v.width;
    r.height = v.height;
    r.yaw = v.yaw;
    r.pitch = v.pitch;
    r.zoom = v.zoom;
    r.roll = v.roll;
    r.fov = v.verticalFov;
    r.nearClip = v.nearClip;
    r.x = v.cameraX;
    r.y = v.cameraY;
    r.z = v.cameraZ;
    r.orbitObject = v.orbitObject;
    r.displayMode = v.displayMode;
    r.axisView = v.axisView;
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
    v.nearClip = r.nearClip;
    v.cameraX = r.x;
    v.cameraY = r.y;
    v.cameraZ = r.z;
    v.orbitObject = r.orbitObject;
    v.displayMode = r.displayMode;
    v.axisView = r.axisView;
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
struct Reply
{
    std::uint32_t magic = Magic, errorBytes = 0, width = 0, height = 0, covered = 0, depth = 0;
    MM::PreviewCamera camera;
    MM::PreviewProjection projection, sceneCamera;
};
static_assert(sizeof(Request) == 76 && sizeof(Reply) == 216 && sizeof(PropOffset) == 16, "ASM wire layout changed");
} // namespace MarcusASM
#endif
