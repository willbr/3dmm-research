#ifndef MM_AUDIO_CLOCK_H
#define MM_AUDIO_CLOCK_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMAudioCounter
    {
        MMU32 low, high;
    } MMAudioCounter;
    MMAudioCounter mm_audio_counter(MMU32 low, MMU32 high);
    MMAudioCounter mm_audio_count_size(size_t);
    size_t mm_audio_count_to_size(MMAudioCounter);
    MMAudioCounter mm_audio_count_add(MMAudioCounter, MMAudioCounter);
    MMAudioCounter mm_audio_count_sub(MMAudioCounter, MMAudioCounter);
    int mm_audio_count_compare(MMAudioCounter, MMAudioCounter);
    MMAudioCounter mm_audio_count_multiply(MMU32, MMU32);
    MMAudioCounter mm_audio_count_divide(MMAudioCounter, MMAudioCounter,
                                                    MMAudioCounter *remainder);
#ifdef __cplusplus
}
#endif
#endif
