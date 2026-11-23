/* Movie sound resources converted from engine msnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SOUND_RESOURCE_H
#define MM_ENGINE_SOUND_RESOURCE_H
#include "chunky_file.h"
#include "chunk_storage.h"
#include "collection_storage.h"
#include "resource_cache.h"
#include "string_value.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_SOUND_TYPE=0x4d534e44, MM_SOUND_MIDI=0x4d494453, MM_SOUND_WAVE=0x57415645 };
enum { MM_SOUND_HEADER_BYTES = 16 };
enum { MM_SOUND_STYLE_EFFECT = 2, MM_SOUND_STYLE_SPEECH = 3, MM_SOUND_STYLE_MUSIC = 4 };
enum { MM_SOUND_CLASS_NONLOOP, MM_SOUND_CLASS_LOOP_WAVE, MM_SOUND_CLASS_LOOP_MIDI };
struct MMMovieSessionGraph;
typedef struct MMSoundHeader
{
    MMI32 style, volume;
    int valid_order, invalid, reserved_nonzero;
} MMSoundHeader;
/* Style and volume use the caller's native byte order, irrespective of the marker.
 * Returns a movie status. A successful read includes unrecognized markers so each
 * caller can apply its compatibility policy. I/O failure leaves the header alone. */
int mm_sound_read_header(const struct MMMovieSessionGraph *, const MMChunkEntry *, int host_big_endian,
                         MMSoundHeader *);
/* The catalog accepts Effects, Speech and Music with a WAVE or MIDI child at
 * any slot. Publishes the style only for an eligible sound. */
int mm_sound_catalog_style(const struct MMMovieSessionGraph *, MMU32 number, int host_big_endian, MMI32 *style);
typedef struct MMSoundResource
{
    MMU32 type,number;
    MMResourceCache *cache;
    MMI32 style,volume;
} MMSoundResource;
typedef struct MMSoundState
{
    MMSoundResource resource;
    MMI32 silent;
    unsigned char invalid;
} MMSoundState;
typedef struct MMSoundOwner MMSoundOwner;
typedef struct MMSoundLastMidi { MMI32 instance; MMU32 type,number; } MMSoundLastMidi;
typedef struct MMSoundResourceServices
{
    void *context; int host_big_endian; MMU32 system;
    void (*invalidate_output)(void *,void *,size_t);
    MMBlockTransform decode;
    void (*graph)(void *,void *file,struct MMMovieSessionGraph *);
    int (*read_name)(void *,void *file,MMResourceCache *,MMU32 type,MMU32 number,MMSoundOwner *);
    void (*publish_invalid)(void *,void *output,int value);
    void (*bad_order)(void *);
    int (*playing)(void *,MMI32 instance);
    void (*stop)(void *,MMI32 queue);
    MMI32 (*play)(void *,const MMSoundResource *,MMI32 queue,MMI32 volume,MMI32 repeat,MMU32 start,MMI32 priority,MMI32 sound_class);
} MMSoundResourceServices;
typedef struct MMSoundResourceHost
{
    const MMSoundResourceServices *services;
    const MMStorageAllocator *objects; /* NULL selects the C heap. */
    MMI32 cache_bytes;
    unsigned name_unit; /* Native one- or two-byte character width. */
    void (*mark_memory)(void *);
    void (*mark_cache)(MMResourceCache *);
} MMSoundResourceHost;
/* The host and providers outlive their sound owners. Each owner holds its
 * counted name in the same allocation. The sound payload's cache is weak;
 * an attached cache entry owns the sound and detached bindings retain it. */
MMSoundOwner *mm_sound_owner_create(const MMSoundResourceHost *);
MMSoundState *mm_sound_owner_state(MMSoundOwner *);
MMStringValue mm_sound_owner_name(MMSoundOwner *);
const MMResourceObject *mm_sound_owner_binding(const MMSoundOwner *);
const MMResourceObjectServices *mm_sound_owner_services(void);
MMI32 mm_sound_owner_references(const MMSoundOwner *);
void mm_sound_owner_retain(MMSoundOwner *);
void mm_sound_owner_release(MMSoundOwner *);
/* Direct load preserves published fields on failure. Invalid sounds retain
 * the previous child and silence fields. Factory estimates precede allocation
 * and factory failure invalidates both outputs before releasing the owner. */
int mm_sound_read(MMSoundOwner *,void *file,MMU32 type,MMU32 number);
/* Graph-only playback and editing can inspect sound state without loading names. */
int mm_sound_read_graph(MMSoundState *,const struct MMMovieSessionGraph *,MMU32 type,MMU32 number,int host_big);
/* Borrow the root bound by the cache. NULL looks up its block by key. */
int mm_sound_fetch(MMResourceCache *,MMU32 type,MMU32 number,MMOwnedBlock *root,void **object,MMI32 *bytes,
                   const MMSoundResourceHost *);
int mm_sound_info(void *file,MMU32 type,MMU32 number,void *invalid_out,void *style_out,void *volume_out,
                  const MMSoundResourceHost *);
enum { MM_SOUND_BAD_MIDI = -1 };
/* Borrow payload and encoded name for the call. Once the parent number is
 * published, later failures remove the sound graph and retain that number. */
int mm_sound_write(const MMReadStream *payload, int wave, MMI32 style, const MMChunkNameValue *name,
                    MMU32 *number, const MMArchiveWriter *);
/* Convert a standard MIDI file to millisecond track bytes in C-owned scratch.
 * Returns MM_SOUND_BAD_MIDI on read/parse failure before creating any chunks,
 * otherwise returns the writer's result. */
int mm_sound_import_midi(const MMReadStream *, const MMChunkNameValue *name, MMU32 *number, const MMArchiveWriter *);
typedef int (*MMSoundWaveImport)(void *context, const MMReadStream *, MMI32 style,
                                 const MMChunkNameValue *, MMU32 *number, const MMArchiveWriter *);
typedef struct MMSoundImportSource
{
    MMReadStream stream;
    MMChunkNameValue name;
    int midi;
    void *context;
    MMSoundWaveImport wave;
} MMSoundImportSource;
/* Import into the native little-endian sound format. The input stream, title
 * and sampled audio provider are borrowed. A negative result is a codec error.
 * A provider may instead report its own diagnostic and return zero. */
int mm_sound_import(const MMSoundImportSource *, MMI32 style, MMU32 *number, const MMArchiveWriter *);
int mm_sound_invalidate(MMSoundState *,const MMResourceObject *,const MMChunkEdit *,const MMSoundResourceServices *);
int mm_sound_owner_invalidate(MMSoundOwner *);
MMI32 mm_sound_class(MMI32 style,int loop);
/* Tool 0=Sounder, 1=Looper, 2=Matcher; MIDI matching is excluded by native bounds. */
MMI32 mm_sound_priority(MMI32 style,int tool);
void mm_sound_play(const MMSoundState *,MMSoundLastMidi *,MMI32 object_id,int loop,int queue,MMI32 volume,MMI32 priority,int actor,MMU32 start,const MMSoundResourceServices *);
#ifdef __cplusplus
}
#endif
#endif
