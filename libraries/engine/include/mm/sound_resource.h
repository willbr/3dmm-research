/* Movie sound resources converted from engine msnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SOUND_RESOURCE_H
#define MM_ENGINE_SOUND_RESOURCE_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_SOUND_TYPE=0x4d534e44, MM_SOUND_MIDI=0x4d494453, MM_SOUND_WAVE=0x57415645 };
typedef struct MMSoundResource
{
    MMU32 type,number;
    void *cache;
    MMI32 style,volume;
} MMSoundResource;
/* Separate slots allow the temporary native facade to retain its original
 * String placement, object size and cache estimate without packed pointers. */
typedef struct MMSoundOwner
{
    MMSoundResource *resource;
    MMI32 *silent;
    unsigned char *invalid;
} MMSoundOwner;
typedef struct MMSoundLastMidi { MMI32 instance; MMU32 type,number; } MMSoundLastMidi;
typedef struct MMSoundChild { MMU32 type,number; } MMSoundChild;
typedef struct MMSoundResourceServices
{
    void *context; int host_big_endian; MMU32 system;
    void (*owner)(void *,void *object,MMSoundOwner *);
    void *(*create)(void *);
    void (*release)(void *,void *);
    MMI32 (*object_size)(void *);
    void (*invalidate_output)(void *,void *,size_t);
    void *(*file)(void *,void *cache);
    void *(*find)(void *,void *file,MMU32 type,MMU32 number);
    int (*unpack)(void *,void *block);
    MMI32 (*block_size)(void *,void *block);
    int (*read)(void *,void *block,void *,MMI32 bytes);
    int (*read_name)(void *,void *file,MMU32 type,MMU32 number,void *object);
    int (*child)(void *,void *file,MMU32 type,MMU32 number,MMSoundChild *);
    void (*publish_invalid)(void *,void *output,int value);
    int (*add)(void *,void *file,const void *header,void *number_out);
    MMI32 (*wave_size)(void *,void *source);
    MMI32 (*midi_size)(void *,void *source);
    int (*add_child)(void *,void *file,MMU32 parent,MMU32 type,MMI32 bytes,MMU32 *child);
    int (*write_midi)(void *,void *source);
    int (*find_wave)(void *,void *file,MMU32 number);
    int (*copy_wave)(void *);
    int (*set_name)(void *,void *file,MMU32 number,const void *name);
    void (*delete_parent)(void *,void *file,MMU32 number);
    void *(*prepare_name)(void *,void *source,const void *name);
    void *(*read_midi)(void *);
    void (*bad_sound)(void *);
    void (*binding)(void *,void *object,void **file,MMU32 *type,MMU32 *number);
    int (*put)(void *,void *file,MMU32 type,MMU32 number,const void *header);
    void (*delete_child)(void *,void *file,MMU32 type,MMU32 number,const MMSoundChild *);
    void (*bad_order)(void *);
    MMI32 (*queue_number)(void *,MMI32 style,MMI32 object,int actor);
    int (*playing)(void *,MMI32 instance);
    void (*stop)(void *,MMI32 queue);
    MMI32 (*play)(void *,const MMSoundResource *,MMI32 queue,MMI32 volume,MMI32 repeat,MMU32 start,MMI32 priority,MMI32 sound_class);
} MMSoundResourceServices;
/* Fresh owners/slots are zeroed; cache pointers are weak. Direct load preserves
 * published fields on failure. The 16-byte MSND envelope has native-order style
 * and volume even with foreign markers: native swapped only the markers using
 * the background mask. Short input now fails before the native out-of-bounds
 * read/assert. Invalid sounds retain old child/silent fields. Factory estimates
 * publish before allocation, failure invalidates outputs before release.
 * Callbacks must not reenter/mutate owners. */
int mm_sound_read(void *object,void *file,MMU32 type,MMU32 number,const MMSoundResourceServices *);
int mm_sound_fetch(void *cache,MMU32 type,MMU32 number,void *object_out,void *size_out,const MMSoundResourceServices *);
int mm_sound_info(void *file,MMU32 type,MMU32 number,void *invalid_out,void *style_out,void *volume_out,const MMSoundResourceServices *);
/* Successful envelopes zero the three undefined native padding bytes. Parent
 * add failure leaves any number it published; all later failures delete parent
 * and child, retaining the output number. MIDI import borrows source/name and
 * releases the parsed stream on every path; parser failure pushes bad-sound. */
int mm_sound_write(void *source,int wave,void *file,MMI32 style,const void *name,void *number_out,const MMSoundResourceServices *);
int mm_sound_copy_midi(void *source,void *file,const void *name,void *number_out,const MMSoundResourceServices *);
int mm_sound_invalidate(void *object,const MMSoundResourceServices *);
MMI32 mm_sound_class(MMI32 style,int loop);
/* Tool 0=Sounder, 1=Looper, 2=Matcher; MIDI matching is excluded by native bounds. */
MMI32 mm_sound_priority(MMI32 style,int tool);
void mm_sound_play(void *object,MMSoundLastMidi *,MMI32 object_id,int loop,int queue,MMI32 volume,MMI32 priority,int actor,MMU32 start,const MMSoundResourceServices *);
#ifdef __cplusplus
}
#endif
#endif
