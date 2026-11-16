#ifndef MARCUS_COLOUR_PICKER_H
#define MARCUS_COLOUR_PICKER_H
#include <array>
#include <vector>

// Hue is in degrees; saturation and perceptual lightness are in [0, 1].
unsigned MarcusOkHSL(float hue, float saturation, float lightness);
std::array<float, 3> MarcusToOkHSL(unsigned rgb);
// Unit triangle: hue (1, 0), white (-.5, -sqrt(3)/2), black (-.5, sqrt(3)/2).
std::array<float, 2> MarcusOkHSLTrianglePoint(float saturation, float lightness);
bool MarcusOkHSLTriangleContains(float x, float y);
std::array<float, 2> MarcusOkHSLTrianglePick(float x, float y);
std::vector<unsigned char> MarcusOkHSLTrianglePixels(float hue, int side);
#endif
