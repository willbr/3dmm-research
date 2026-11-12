/* Actor persistence converted from engine actor_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_DATA_H
#define THREE_MM_ENGINE_ACTOR_DATA_H
#include "read_stream.h"
#include "tag.h"
#include "text.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_ACTOR_HEADER_BYTES = 44,
        THREE_MM_ACTOR_OLD_HEADER_BYTES = 40,
        THREE_MM_ACTOR_EVENT_BYTES = 20,
        THREE_MM_ACTOR_ROUTE_BYTES = 16
    };
    enum ThreeMMActorOrder
    {
        THREE_MM_ACTOR_NATIVE,
        THREE_MM_ACTOR_OTHER,
        THREE_MM_ACTOR_INVALID_ORDER
    };
    enum ThreeMMActorEventKind
    {
        THREE_MM_ACTOR_ADD,
        THREE_MM_ACTOR_ACTION,
        THREE_MM_ACTOR_COSTUME,
        THREE_MM_ACTOR_ROTATE_FORWARD,
        THREE_MM_ACTOR_STRETCH,
        THREE_MM_ACTOR_SIZE,
        THREE_MM_ACTOR_SOUND,
        THREE_MM_ACTOR_MOVE,
        THREE_MM_ACTOR_FREEZE,
        THREE_MM_ACTOR_TWEAK,
        THREE_MM_ACTOR_STEP,
        THREE_MM_ACTOR_REMOVE,
        THREE_MM_ACTOR_ROTATE_HERE
    };
    typedef struct ThreeMMActorHeader
    {
        unsigned short order, system;
        ThreeMMU32 translation[3]; /* BRender scalar bits, without numeric conversion. */
        ThreeMMI32 actor_id, first_frame, last_frame, source_id;
        ThreeMMU32 wire_pointer_pad, template_type, template_number;
    } ThreeMMActorHeader;
    typedef char ThreeMMActorHeaderSize[(sizeof(ThreeMMActorHeader) == 44) ? 1 : -1];
    typedef struct ThreeMMActorCostume
    {
        ThreeMMI32 body_set, costume, custom;
        ThreeMMTag tag;
    } ThreeMMActorCostume;
    typedef struct ThreeMMActorSound
    {
        ThreeMMI32 loop, queue, volume, cel, style, no_sound;
        ThreeMMU32 child;
        ThreeMMTag tag;
    } ThreeMMActorSound;
    /* Caller supplies 44 bytes of native header storage (or ThreeMMActorHeader).
     * Accepts 40/44 byte wire records, expands the old layout and swaps scalar bits.
     * Like the legacy reader, failed reads may modify output; invalid byte order
     * leaves the read/expanded bytes visible. Old last_frame becomes INT32_MAX only
     * after byte-order validation succeeds. All other header values are accepted. */
    int three_mm_actor_read_header(const ThreeMMReadStream *, void *header);
    int three_mm_actor_adjust_header_id(void *header, ThreeMMI32 delta);
    typedef struct ThreeMMActorHeaderIO
    {
        void *context;
        int (*read)(void *, ThreeMMU32 number, ThreeMMReadStream *);
        int (*put)(void *, ThreeMMU32 number, const void *native_header, ThreeMMU32 bytes);
    } ThreeMMActorHeaderIO;
    int three_mm_actor_write_header(ThreeMMU32 number, const void *header, const ThreeMMActorHeaderIO *);
    int three_mm_actor_adjust_id(ThreeMMU32 number, ThreeMMI32 delta, const ThreeMMActorHeaderIO *);
    typedef struct ThreeMMActorRouteServices
    {
        ThreeMMU32 (*width)(void *);
        ThreeMMU32 (*count)(void *);
        void *(*data)(void *);
        void (*release)(void *);
    } ThreeMMActorRouteServices;
    /* Consumes route on rejection; caller owns the returned route on success.
     * Empty routes never request a first-row pointer. Scalar bits stay unchanged
     * except for foreign-order word swaps. */
    void *three_mm_actor_finish_route(void *route, int order, const ThreeMMActorRouteServices *);
    typedef struct ThreeMMActorEventServices
    {
        void *context;
        ThreeMMU32 (*fixed_width)(void *);
        ThreeMMU32 (*count)(void *);
        void *(*fixed)(void *, ThreeMMU32);
        ThreeMMI32 (*size)(void *, ThreeMMU32);
        void *(*data)(void *, ThreeMMU32);
        void *(*new_group)(void *, ThreeMMU32 fixed_bytes);
        int (*insert)(void *group, ThreeMMU32 index, ThreeMMU32 bytes, const void *variable, const void *fixed);
        void (*release)(void *);
    } ThreeMMActorEventServices;
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
    int three_mm_actor_valid_events(void *group, int wire, int order, const ThreeMMActorEventServices *);
    void *three_mm_actor_read_events(void *wire, int order, const ThreeMMActorEventServices *);
    void *three_mm_actor_write_events(void *runtime, const ThreeMMActorEventServices *);
#ifdef __cplusplus
}
#endif
#endif
