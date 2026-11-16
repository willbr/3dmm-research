#ifndef MARCUS_STORYBOARD_TOOLS_H
#define MARCUS_STORYBOARD_TOOLS_H
#include "storyboard.h"
#include "camera_guide.h"

MarcusProjection MarcusBoardProjection(const MarcusBoardAngle &angle);
struct MarcusBoardLightWidget
{
    MarcusCameraGuide shape = {}, direction = {};
    std::array<float, 3> centre = {};
    bool visible = false;
    float scale = 0;
};
MarcusBoardLightWidget MarcusBoardBuildLightWidget(const MarcusProjection &view, const MarcusBoardLight &light, float viewportHeight);
// Column-major rigid transform for the shared ImGuizmo controls.
void MarcusBoardLightTransform(const MarcusBoardLight &light, float matrix[16]);
bool MarcusBoardLightFromTransform(const MarcusBoardLight &original, const float matrix[16], bool rotate, MarcusBoardLight &light);
// Fade the live guide as projected grid cells approach a pixel in either direction.
// Coordinates are normalized image positions; viewport dimensions are display pixels.
class MarcusBoardGridFade
{
    MarcusProjection view = {};
    double tangent = 0, scaleX = 0, scaleY = 0;
public:
    MarcusBoardGridFade(const MarcusBoardAngle &angle, float width, float height);
    float Opacity(double x, double y) const;
};
// Split the live ground guide at occluders. Scene depths are camera-forward distances.
std::vector<std::array<double, 4>> MarcusBoardVisibleGridLines(const MarcusBoardAngle &angle,
    const std::vector<float> &depth, int width, int height);
#endif
