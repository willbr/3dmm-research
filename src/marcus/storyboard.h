#ifndef MARCUS_STORYBOARD_H
#define MARCUS_STORYBOARD_H
#include "player.h"
#ifdef __cplusplus
#include "movie_input.h"
#include "mm/archive.h"
#include "mm/v3dmm.h"
#include <array>
#include <map>

constexpr int MarcusBoardWidth = 544, MarcusBoardHeight = 306;
constexpr unsigned MarcusBoardChunk = 0x53544244; // STBD, versioned editor data.
constexpr unsigned MarcusBoardColours[] = {0xd3d4d6, 0x000000, 0xffffff,
    0x9ebfd9, 0xdea5a2, 0xe6d8a0, 0xaac9ad};
struct MarcusBoardAngle
{
    // GE-Black's first camera position and lens, with level pitch.
    MarcusCamera camera = {0, 20, 20, 0, 0, 5};
    float roll = 0, fov = 41.5338134765625f, nearClip = 1, farClip = 5000;
    std::vector<unsigned char> pixels = std::vector<unsigned char>(MarcusBoardWidth * MarcusBoardHeight, 0);
};
struct MarcusBoardLight
{
    float x = 0, y = 10, z = 10, yaw = -30, pitch = -35, intensity = .8f;
    int point = 0;
};
struct MarcusStoryboard
{
    std::vector<MarcusBoardAngle> angles = {MarcusBoardAngle{}};
    std::vector<MarcusBoardLight> lights = {MarcusBoardLight{}};
};
struct MarcusBoardResource
{
    std::shared_ptr<const MarcusStoryboard> board;
    std::shared_ptr<MarcusMovieFile> file;
    unsigned long background = 0;
    long source = 0;
};
using MarcusStoryboards = std::map<unsigned long, MarcusBoardResource>;
bool MarcusBoardValid(const MarcusStoryboard &board);
std::vector<unsigned char> MarcusEncodeBoard(const MarcusStoryboard &board);
MarcusStoryboard MarcusDecodeBoard(const std::vector<unsigned char> &bytes);
MarcusBoardResource MarcusBoardContent(const MarcusStoryboard &board, long source, const std::string &resources, const std::string &content);
using MarcusBoardPalette = std::array<unsigned, 256>;
struct MarcusBoardInk { unsigned char first = 0, second = 0, amount = 0; unsigned rgb = 0; };
std::array<MarcusBoardInk, 7> MarcusBoardColourMap(const MarcusBoardPalette &palette);
unsigned char MarcusBoardPaintIndex(const MarcusBoardInk &ink, int x, int y);
bool MarcusGetStoryboardColours(MarcusPlayer *player, std::array<unsigned, 7> &colours);
void MarcusWriteStoryboardBundle(const MarcusMovieFile &movie, const MarcusStoryboards &boards,
    const std::string &resources, const std::string &content, std::vector<MM::V3dmm::Dependency> dependencies,
    const std::vector<std::filesystem::path> &portableContent, const std::filesystem::path &destination);
MarcusStoryboards MarcusReadBoards(MM::Archive &archive, const std::shared_ptr<MarcusMovieFile> &file);
// Drawing uses integer stamps and interpolation, with no smoothing or alpha blending.
void MarcusBoardStroke(MarcusBoardAngle &angle, int x0, int y0, int x1, int y1, int size, unsigned char colour);
// Clipped ground-grid segments in normalized image coordinates, shared by the guide and baking.
std::vector<std::array<double, 4>> MarcusBoardGridLines(const MarcusBoardAngle &angle);
void MarcusBoardBakeGrid(MarcusBoardAngle &angle, unsigned char colour);
bool MarcusGetStoryboard(MarcusPlayer *player, MarcusStoryboard &board);
bool MarcusRenderStoryboard(MarcusPlayer *player, const MarcusStoryboard &board, unsigned long camera, MarcusImage &image);
bool MarcusApplyStoryboard(MarcusPlayer *player, const MarcusStoryboard &board, bool newScene,
    unsigned long camera, const char *resources, const char *content);
extern "C" {
#endif
    int marcus_is_storyboard(const MarcusPlayer *player);
    /* Export a movie copy. Storyboards/portable content require a bundled .vmm. */
    int marcus_save_copy(MarcusPlayer *player, const char *path);
#ifdef __cplusplus
}
#endif
#endif
