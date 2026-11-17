/* Actor persistence converted from engine actor_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_DATA_H
#define MM_ENGINE_ACTOR_DATA_H
#include "read_stream.h"
#include "chunky_file.h"
#include "tag.h"
#include "text.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_ACTOR_HEADER_BYTES = 44,
        MM_ACTOR_OLD_HEADER_BYTES = 40,
        MM_ACTOR_EVENT_BYTES = 20,
        MM_ACTOR_ROUTE_BYTES = 16
    };
    enum MMActorOrder
    {
        MM_ACTOR_NATIVE,
        MM_ACTOR_OTHER,
        MM_ACTOR_INVALID_ORDER
    };
    enum MMActorEventKind
    {
        MM_ACTOR_ADD,
        MM_ACTOR_ACTION,
        MM_ACTOR_COSTUME,
        MM_ACTOR_ROTATE_FORWARD,
        MM_ACTOR_STRETCH,
        MM_ACTOR_SIZE,
        MM_ACTOR_SOUND,
        MM_ACTOR_MOVE,
        MM_ACTOR_FREEZE,
        MM_ACTOR_TWEAK,
        MM_ACTOR_STEP,
        MM_ACTOR_REMOVE,
        MM_ACTOR_ROTATE_HERE
    };
    typedef struct MMActorHeader
    {
        unsigned short order, system;
        MMU32 translation[3]; /* BRender scalar bits, without numeric conversion. */
        MMI32 actor_id, first_frame, last_frame, source_id;
        MMU32 wire_pointer_pad, template_type, template_number;
    } MMActorHeader;
    typedef char MMActorHeaderSize[(sizeof(MMActorHeader) == 44) ? 1 : -1];
    typedef struct MMActorCostume
    {
        MMI32 body_set, costume, custom;
        MMTag tag;
    } MMActorCostume;
    typedef struct MMActorSound
    {
        MMI32 loop, queue, volume, cel, style, no_sound;
        MMU32 child;
        MMTag tag;
    } MMActorSound;
    /* Caller supplies 44 bytes of native header storage (or MMActorHeader).
     * Accepts 40/44 byte wire records, expands the old layout and swaps scalar bits.
     * Like the legacy reader, failed reads may modify output; invalid byte order
     * leaves the read/expanded bytes visible. Old last_frame becomes INT32_MAX only
     * after byte-order validation succeeds. All other header values are accepted. */
    int mm_actor_read_header(const MMReadStream *, void *header);
    /* Embedded actors use the TMPL link at slot zero after import/renumbering.
     * External sources skip graph lookup. A missing link leaves the stored
     * template number available as a fallback. Stream consumers can require
     * the linked target to exist before replacing the stored number. */
    int mm_actor_template_link(const MMChunkAccess *, MMU32 actor, MMI32 source, int require_target,
                               MMU32 *template_number);
    int mm_actor_adjust_header_id(void *header, MMI32 delta);
    typedef struct MMActorHeaderIO
    {
        void *context;
        int (*read)(void *, MMU32 number, MMReadStream *);
        int (*put)(void *, MMU32 number, const void *native_header, MMU32 bytes);
    } MMActorHeaderIO;
    int mm_actor_write_header(MMU32 number, const void *header, const MMActorHeaderIO *);
    int mm_actor_adjust_id(MMU32 number, MMI32 delta, const MMActorHeaderIO *);
    typedef struct MMActorRouteServices
    {
        MMU32 (*width)(void *);
        MMU32 (*count)(void *);
        void *(*data)(void *);
        void (*release)(void *);
    } MMActorRouteServices;
    /* Consumes route on rejection; caller owns the returned route on success.
     * Empty routes never request a first-row pointer. Scalar bits stay unchanged
     * except for foreign-order word swaps. */
    void *mm_actor_finish_route(void *route, int order, const MMActorRouteServices *);
    typedef struct MMActorEventServices
    {
        void *context;
        MMU32 (*fixed_width)(void *);
        MMU32 (*count)(void *);
        void *(*fixed)(void *, MMU32);
        MMI32 (*size)(void *, MMU32);
        void *(*data)(void *, MMU32);
        void *(*new_group)(void *, MMU32 fixed_bytes);
        int (*insert)(void *group, MMU32 index, MMU32 bytes, const void *variable, const void *fixed);
        void (*release)(void *);
    } MMActorEventServices;
    /* Layout checks only: retain payload values/frame-order acceptance. Runtime
     * Costume/Sound layouts use the C records above; native adapters assert ABI
     * equality. Other payloads are identical in memory and on disk. Add's three
     * 16-bit angles are swapped separately; its final two padding bytes are opaque.
     * Read consumes wire on every outcome, releasing it before partial runtime
     * storage. Write borrows runtime, releasing only partial output on failure.
     * Neither operation opens/closes/retains tags. Read sets cache=NULL; write zeros
     * wire pointer slots and never reads runtime cache identity. new_group must
     * return independent empty storage. Callbacks are synchronous and must not
     * re-enter these operations or mutate their borrowed source groups. */
    int mm_actor_valid_events(void *group, int wire, int order, const MMActorEventServices *);
    void *mm_actor_read_events(void *wire, int order, const MMActorEventServices *);
    void *mm_actor_write_events(void *runtime, const MMActorEventServices *);
#ifdef __cplusplus
}
#endif
#endif
