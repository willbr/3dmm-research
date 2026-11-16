#ifndef MARCUS_STORYBOARD_TOOLS_H
#define MARCUS_STORYBOARD_TOOLS_H
#include "storyboard.h"
#include "camera_guide.h"

MarcusProjection MarcusBoardProjection(const MarcusBoardAngle &angle);
struct MarcusBoardLightWidget
{
    MarcusCameraGuide shape = {}, direction = {};
    std::array<MarcusCameraGuide, 3> axes = {};
    // Centre, X, Y, Z and directional-light aim handle, in normalized image coordinates.
    std::array<std::array<float, 3>, 5> handles = {};
    std::array<bool, 5> visible = {};
    float scale = 0;
};
MarcusBoardLightWidget MarcusBoardBuildLightWidget(const MarcusProjection &view, const MarcusBoardLight &light, float viewportHeight);
bool MarcusBoardDragLight(const MarcusProjection &view, const MarcusBoardLight &original, int handle, float scale,
    float startX, float startY, float x, float y, MarcusBoardLight &light);
// Split the live ground guide at occluders. Scene depths are camera-forward distances.
std::vector<std::array<double, 4>> MarcusBoardVisibleGridLines(const MarcusBoardAngle &angle,
    const std::vector<float> &depth, int width, int height);
#endif
