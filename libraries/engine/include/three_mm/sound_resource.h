/* Movie sound resources converted from engine msnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SOUND_RESOURCE_H
#define THREE_MM_ENGINE_SOUND_RESOURCE_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { THREE_MM_SOUND_TYPE=0x4d534e44, THREE_MM_SOUND_MIDI=0x4d494453, THREE_MM_SOUND_WAVE=0x57415645 };
typedef struct ThreeMMSoundResource
{
    ThreeMMU32 type,number;
    void *cache;
    ThreeMMI32 style,volume;
} ThreeMMSoundResource;
/* Separate slots allow the temporary native facade to retain its original
 * String placement, object size and cache estimate without packed pointers. */
typedef struct ThreeMMSoundOwner
{
    ThreeMMSoundResource *resource;
    ThreeMMI32 *silent;
    unsigned char *invalid;
} ThreeMMSoundOwner;
typedef struct ThreeMMSoundLastMidi { ThreeMMI32 instance; ThreeMMU32 type,number; } ThreeMMSoundLastMidi;
typedef struct ThreeMMSoundChild { ThreeMMU32 type,number; } ThreeMMSoundChild;
typedef struct ThreeMMSoundResourceServices
{
    void *context; int host_big_endian; ThreeMMU32 system;
    void (*owner)(void *,void *object,ThreeMMSoundOwner *);
    void *(*create)(void *);
    void (*release)(void *,void *);
    ThreeMMI32 (*object_size)(void *);
    void (*invalidate_output)(void *,void *,size_t);
    void *(*file)(void *,void *cache);
    void *(*find)(void *,void *file,ThreeMMU32 type,ThreeMMU32 number);
    int (*unpack)(void *,void *block);
    ThreeMMI32 (*block_size)(void *,void *block);
    int (*read)(void *,void *block,void *,ThreeMMI32 bytes);
    int (*read_name)(void *,void *file,ThreeMMU32 type,ThreeMMU32 number,void *object);
    int (*child)(void *,void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMSoundChild *);
    void (*publish_invalid)(void *,void *output,int value);
    int (*add)(void *,void *file,const void *header,void *number_out);
    ThreeMMI32 (*wave_size)(void *,void *source);
    ThreeMMI32 (*midi_size)(void *,void *source);
    int (*add_child)(void *,void *file,ThreeMMU32 parent,ThreeMMU32 type,ThreeMMI32 bytes,ThreeMMU32 *child);
    int (*write_midi)(void *,void *source);
    int (*find_wave)(void *,void *file,ThreeMMU32 number);
    int (*copy_wave)(void *);
    int (*set_name)(void *,void *file,ThreeMMU32 number,const void *name);
    void (*delete_parent)(void *,void *file,ThreeMMU32 number);
    void *(*prepare_name)(void *,void *source,const void *name);
    void *(*read_midi)(void *);
    void (*bad_sound)(void *);
    void (*binding)(void *,void *object,void **file,ThreeMMU32 *type,ThreeMMU32 *number);
    int (*put)(void *,void *file,ThreeMMU32 type,ThreeMMU32 number,const void *header);
    void (*delete_child)(void *,void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMSoundChild *);
    void (*bad_order)(void *);
    ThreeMMI32 (*queue_number)(void *,ThreeMMI32 style,ThreeMMI32 object,int actor);
    int (*playing)(void *,ThreeMMI32 instance);
    void (*stop)(void *,ThreeMMI32 queue);
    ThreeMMI32 (*play)(void *,const ThreeMMSoundResource *,ThreeMMI32 queue,ThreeMMI32 volume,ThreeMMI32 repeat,ThreeMMU32 start,ThreeMMI32 priority,ThreeMMI32 sound_class);
} ThreeMMSoundResourceServices;
/* Fresh owners/slots are zeroed; cache pointers are weak. Direct load preserves
 * published fields on failure. The 16-byte MSND envelope has native-order style
 * and volume even with foreign markers: native swapped only the markers using
 * the background mask. Short input now fails before the native out-of-bounds
 * read/assert. Invalid sounds retain old child/silent fields. Factory estimates
 * publish before allocation, failure invalidates outputs before release.
 * Callbacks must not reenter/mutate owners. */
int three_mm_sound_read(void *object,void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMSoundResourceServices *);
int three_mm_sound_fetch(void *cache,ThreeMMU32 type,ThreeMMU32 number,void *object_out,void *size_out,const ThreeMMSoundResourceServices *);
int three_mm_sound_info(void *file,ThreeMMU32 type,ThreeMMU32 number,void *invalid_out,void *style_out,void *volume_out,const ThreeMMSoundResourceServices *);
/* Successful envelopes zero the three undefined native padding bytes. Parent
 * add failure leaves any number it published; all later failures delete parent
 * and child, retaining the output number. MIDI import borrows source/name and
 * releases the parsed stream on every path; parser failure pushes bad-sound. */
int three_mm_sound_write(void *source,int wave,void *file,ThreeMMI32 style,const void *name,void *number_out,const ThreeMMSoundResourceServices *);
int three_mm_sound_copy_midi(void *source,void *file,const void *name,void *number_out,const ThreeMMSoundResourceServices *);
int three_mm_sound_invalidate(void *object,const ThreeMMSoundResourceServices *);
ThreeMMI32 three_mm_sound_class(ThreeMMI32 style,int loop);
/* Tool 0=Sounder, 1=Looper, 2=Matcher; MIDI matching is excluded by native bounds. */
ThreeMMI32 three_mm_sound_priority(ThreeMMI32 style,int tool);
void three_mm_sound_play(void *object,ThreeMMSoundLastMidi *,ThreeMMI32 object_id,int loop,int queue,ThreeMMI32 volume,ThreeMMI32 priority,int actor,ThreeMMU32 start,const ThreeMMSoundResourceServices *);
#ifdef __cplusplus
}
#endif
#endif
