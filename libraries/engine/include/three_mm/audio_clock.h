#ifndef THREE_MM_AUDIO_CLOCK_H
#define THREE_MM_AUDIO_CLOCK_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMAudioCounter
    {
        ThreeMMU32 low, high;
    } ThreeMMAudioCounter;
    ThreeMMAudioCounter three_mm_audio_counter(ThreeMMU32 low, ThreeMMU32 high);
    ThreeMMAudioCounter three_mm_audio_count_size(size_t);
    size_t three_mm_audio_count_to_size(ThreeMMAudioCounter);
    ThreeMMAudioCounter three_mm_audio_count_add(ThreeMMAudioCounter, ThreeMMAudioCounter);
    ThreeMMAudioCounter three_mm_audio_count_sub(ThreeMMAudioCounter, ThreeMMAudioCounter);
    int three_mm_audio_count_compare(ThreeMMAudioCounter, ThreeMMAudioCounter);
    ThreeMMAudioCounter three_mm_audio_count_multiply(ThreeMMU32, ThreeMMU32);
    ThreeMMAudioCounter three_mm_audio_count_divide(ThreeMMAudioCounter, ThreeMMAudioCounter,
                                                    ThreeMMAudioCounter *remainder);
#ifdef __cplusplus
}
#endif
#endif
