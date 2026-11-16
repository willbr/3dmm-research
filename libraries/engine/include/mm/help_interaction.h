/* Help topic commands, cursor selection and script lifetime policy. */
#ifndef MM_ENGINE_HELP_INTERACTION_H
#define MM_ENGINE_HELP_INTERACTION_H
#include "help_text.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMHelpInteraction
    {
        unsigned group;
        MMU32 topic, flags;
        MMI32 base;
    } MMHelpInteraction;
    typedef struct MMHelpMouse
    {
        MMI32 x, y;
        MMU32 flags;
        int down;
    } MMHelpMouse;
    typedef struct MMHelpKey
    {
        MMI32 id;
        MMU32 flags, character;
    } MMHelpKey;
    typedef struct MMHelpInteractionServices
    {
        void *context;
        void (*mouse)(void *, MMHelpMouse *), (*key)(void *, MMHelpKey *), (*publish_flags)(void *, MMU32);
        int (*adjust)(void *, MMU32, MMU32 *);
        int (*hit)(void *, MMI32 x, MMI32 y, unsigned *group, MMU32 *topic);
        void (*check_tracking)(void *, int down);
        int (*track)(void *, int start), (*cursor)(void *, MMU32);
        int (*run)(void *, unsigned group, MMU32 flags, MMI32 id, MMU32 character, MMU32 topic, MMI32 *result);
        int (*do_hit)(void *, unsigned group, MMU32 topic, MMU32 flags, MMI32 id);
        int (*balloon)(void *, MMU32 topic);
        int (*base_point)(void *, MMI32 x, MMI32 y), (*locate)(void *, MMI32 x, MMI32 y, MMI32 *cp);
        int (*group_at)(void *, MMI32 cp);
        void *(*parent)(void *, void *node);
        int (*kid)(void *, void *node), (*set_cursor)(void *, void *node, MMU32 flags);
    } MMHelpInteractionServices;
    typedef struct MMHelpScriptServices
    {
        void *context;
        void (*reset_return)(void *);
        int (*number)(void *, MMU32 *), (*fetch)(void *, MMU32, void **owned), (*create)(void *, void **owned);
        void (*snapshot)(void *);
        int (*run)(void *, void *interpreter, void *script, const MMI32 args[5]);
        void (*release)(void *, int interpreter, void *);
        int (*alive)(void *);
    } MMHelpScriptServices;
    /* Synchronous services return 1/0 or -1/-2; no exceptions cross C. Primitive
     * command/state publication, hierarchy access, diagnostics, identity capture,
     * return reset and owner release never throw. Commands and state remain live
     * through callbacks and are re-read at their native stages. Only run/do_hit may
     * destroy the topic; a false run result forbids subsequent owner access. */
    void mm_help_interaction_init(MMHelpInteraction *);
    int mm_help_track(MMHelpInteraction *, const MMHelpInteractionServices *);
    int mm_help_bad_key(MMHelpInteraction *, const MMHelpInteractionServices *);
    int mm_help_mouse_move(const MMHelpInteractionServices *);
    int mm_help_hit(unsigned group, MMU32 topic, MMU32 flags, MMI32 id, const MMHelpInteractionServices *);
    int mm_help_point(MMI32 x, MMI32 y, const MMHelpInteractionServices *);
    int mm_help_group_point(MMI32 x, MMI32 y, const MMHelpInteractionServices *);
    int mm_help_cursor(void *self, MMU32 flags, const MMHelpInteractionServices *);
    /* Script parameters narrow character to its low byte. Return output clears
     * before lookup and again on ordinary run failure. Missing scripts/interpreters
     * mean alive. Identity is captured after interpreter creation; interpreter release
     * precedes the identity lookup and script release follows it. The topic may be
     * destroyed by run: subsequent services use only captured world/grid/identity.
     * Private owners release on exception exits; other partial outputs remain. */
    int mm_help_script(unsigned group, MMU32 flags, MMI32 id, MMU32 character, MMU32 topic,
                       const MMHelpScriptServices *);
#ifdef __cplusplus
}
#endif
#endif
