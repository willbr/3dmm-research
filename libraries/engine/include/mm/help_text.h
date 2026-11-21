/* Help topic wire data, argument extensions and document edit transactions. */
#ifndef MM_ENGINE_HELP_TEXT_H
#define MM_ENGINE_HELP_TEXT_H
#include "collections.h"
#include "text_properties.h"
#ifdef __cplusplus
extern "C"
{
#endif
#define MM_HELP_TOPIC 0x48544f50UL
#define MM_HELP_BITMAP 0x4d424d50UL
#define MM_HELP_BUTTON 0x474f4b44UL
#define MM_HELP_EDIT 0x45444954UL
    typedef struct MMHelpTopic
    {
        MMU32 balloon;
        MMI32 hid, target;
        MMU32 script;
        MMI32 width, height;
        MMU32 sound_type, sound_number;
    } MMHelpTopic;
    typedef struct MMHelpArgumentServices
    {
        void *context;
        int (*base)(void *, MMI32 index, unsigned kind, int order, int system);
        MMGroup (*arguments)(void *);
    } MMHelpArgumentServices;
    enum
    {
        MM_HELP_FIRST,
        MM_HELP_END,
        MM_HELP_GROUP,
        MM_HELP_TARGET
    };
    typedef struct MMHelpEditServices
    {
        void *context;
        MMTextPropertyServices properties;
        MMI32 (*characters)(void *);
        int (*undo)(void *, MMI32 first, MMI32 end, MMI32 inserted);
        int (*reserve)(void *, MMI32), (*cancel)(void *), (*commit)(void *);
        int (*group_name)(void *, void *buffer, MMI32 capacity, MMI32 *bytes);
        MMGroup (*arguments)(void *);
        int (*read_name)(void *, const void *, MMI32 bytes), (*clear_name)(void *);
        void (*publish)(void *, int kind, MMU32 value);
        int (*allocate)(void *, MMI32 bytes, void **owned);
        void (*free)(void *, void *);
        int (*insert)(void *, const void *, MMI32 bytes, MMI32 at, MMI32 deleted);
        void (*validate)(void *), (*bad_group)(void *);
        int (*notify)(void *, MMI32 at, MMI32 span), (*dirty)(void *);
    } MMHelpEditServices;
    typedef struct MMHelpReadServices
    {
        void *context;
        int (*forest)(void *, void *, MMU32 type, MMU32 number);
        int (*open_forest)(void *, void *, MMU32 type, MMU32 number, void **owned);
        int (*first)(void *, void *, MMU32 type, MMU32 *number);
        void (*retain)(void *, void *), (*release)(void *, void *);
        int (*header)(void *, void *, MMU32 type, MMU32 number, void *bytes, MMI32 *count);
        int (*child)(void *, void *, MMU32 type, MMU32 number, MMTextChunk *);
        int (*rich)(void *, void *, const MMTextChunk *, int copy);
        int (*internal)(void *), (*suspend)(void *);
        MMI32 (*characters)(void *);
        int (*character)(void *, MMI32 cp, MMU32 *);
        int (*string)(void *, MMI32 id, MMMovieTitle *);
        int (*replace)(void *, const MMMovieTitle *, MMI32 cp, MMI32 deleted);
        void (*missing)(void *), (*validate)(void *);
    } MMHelpReadServices;
    typedef struct MMHelpSaveServices
    {
        void *context;
        void (*publish_type)(void *, MMU32);
        int (*add)(void *, MMU32 type, MMU32 *number);
        int (*write)(void *, const unsigned char bytes[36]);
        int (*rich)(void *, MMTextChunk *);
        int (*adopt)(void *, const MMTextChunk *, const MMTextChunk *);
        int (*erase)(void *, const MMTextChunk *);
        void (*error)(void *);
    } MMHelpSaveServices;
    /* Valid native ranges and initialized owners. Services are synchronous 1/0 or
     * -1/-2 and never throw across C; primitive group access/locks/publication,
     * ref releases, validation and diagnostics never throw. Topic decoding accepts
     * 28/36 bytes, ignores the system short, and swaps only the first six topic
     * words: newer sound fields retain the original unswapped behavior. */
    void mm_help_topic_init(MMHelpTopic *);
    int mm_help_topic_decode(MMHelpTopic *, const void *, MMI32 bytes, int native_big);
    void mm_help_topic_encode(const MMHelpTopic *, int system, int big, unsigned char bytes[36]);
    int mm_help_open_argument(MMI32 index, unsigned kind, int order, int system, int native_big,
                              const MMHelpArgumentServices *);
    int mm_help_group(MMI32 first, MMI32 end, unsigned group, MMU32 topic, int big, const MMHelpEditServices *);
    int mm_help_grouped(MMI32 cp, int big, int name, const MMHelpEditServices *);
    int mm_help_insert(int button, MMU32 number, MMU32 topic, const void *extra, MMI32 bytes, MMI32 at, MMI32 deleted,
                       int big, const MMHelpEditServices *);
    int mm_help_topic_set(MMHelpTopic *, const MMHelpTopic *, const MMHelpEditServices *);
    /* Read keeps expansion internal/undo-suspended, skips inserted placeholders,
     * refreshes length only after successful replacement and publishes topic last.
     * Ordinary edit failures continue. Save preserves child-then-parent deletion
     * and early root publication. Exceptions keep other partial effects; private
     * file/buffer owners and group locks release before native rethrow. */
    int mm_help_read(MMHelpTopic *, void *file, MMU32 type, MMU32 number, MMU32 flags, int strings, unsigned unit,
                     int big, const MMHelpReadServices *);
    int mm_help_save(const MMHelpTopic *, MMTextChunk *, int system, int big, const MMHelpSaveServices *);
#ifdef __cplusplus
}
#endif
#endif
