#ifndef MM_NATIVE_SOUND_H
#define MM_NATIVE_SOUND_H

#include <cstdint>

namespace MM
{
class Archive;
bool ReadSoundCatalogStyle(const Archive &, std::uint32_t number, std::int32_t &style);
}
#endif
