/* Template action/accessory operations converted from engine tmpl.cpp/mtrl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TEMPLATE_RUNTIME_H
#define MM_ENGINE_TEMPLATE_RUNTIME_H
#include "template_create.h"
#include "resource_cache.h"
#ifdef __cplusplus
extern "C" {
#endif
enum MMTemplateResourceKind { MM_TEMPLATE_ACTION, MM_TEMPLATE_MODEL, MM_TEMPLATE_CUSTOM, MM_TEMPLATE_RESOURCE_COUNT };
enum { MM_TEMPLATE_BMDL=0x424d444c };
enum { MM_TEMPLATE_KEEP_MODEL = -1 };
/* The cel count must be positive. */
MMI32 mm_template_wrap_cel(MMI32 cel, MMI32 count);
struct MMMovieAsset;
struct MMActorMatrix;
typedef struct MMTemplateRuntimeServices
{
    MMTemplateCreateServices storage;
    const MMResourceObject *resource;
    MMResourceBlockReader action_reader, model_reader;
    const MMResourceObjectServices *action_objects;
    int (*name)(void *,void *file,MMU32,MMU32,void *out);
    void *(*generated_action)(void *,void *object,MMI32 id);
    void *(*generated_model)(void *,void *object,MMU32 id,struct MMMovieAsset *);
    int (*allocate_cel)(void *,size_t bytes,void **);
    MMI32 (*cel_count)(void *,void *action);
    int (*distance)(void *,void *action,MMI32 cel,MMI32 *);
    MMU32 (*flags)(void *,void *action);
    int (*part)(void *,void *action,MMI32 cel,MMI32 part,MMI32 *model,MMI32 *matrix);
    int (*matrix)(void *,void *action,MMI32 matrix,struct MMActorMatrix *);
    void (*set_model)(void *,void *body,MMI32 part,void *model);
    void (*set_matrix)(void *,void *body,MMI32 part,const struct MMActorMatrix *);
    int (*sound)(void *,void *action,MMI32 cel,void *tag);
    void (*sound_exists)(void *,void *out,int exists);
} MMTemplateRuntimeServices;
/* Use a cache-owned C action for archive lookups. The host supplies the block
 * decoder and publishes any sound tag into its output representation. */
void mm_template_use_action_resource(MMTemplateRuntimeServices *, MMResourceBlockReader);
/* Part counts fit the native signed-short loop range. Parent, set and costume
 * indices are valid. The action readers return one when they publish a value. Scalar
 * and matrix elements are signed 32-bit fixed-point representations. Callbacks
 * do not reenter or mutate unrelated state. Archive templates borrow their
 * current resource binding. Generated and in-memory templates can supply
 * action and model providers. The model provider supplies a key in its
 * owning cache. Both paths return retained resources. The cel
 * allocator supplies zeroed, suitably aligned storage. */
/* Opaque scalar output pointers hold exactly four bytes; representation copies
 * bridge native Windows long/unsigned long and C int/unsigned int storage
 * without incompatible typed pointer aliasing. */
/* Resolve a child in the template's own archive and fetch it under the same
 * reader key used by the cache. A successful fetch may publish the asset key. */
void *mm_template_fetch_child(const MMResourceObject *, MMU32 id, int kind, MMResourceBlockReader,
                             struct MMMovieAsset *asset);
int mm_template_action_name(MMI32 id,void *out,const MMTemplateRuntimeServices *);
/* Evaluate the cel and fetch every model before publishing distance or modifying the body. A nil
 * model ID preserves the accessory model but still applies its cel matrix.
 * Failure leaves body/distance unchanged. Release/clear models in part order,
 * free the temporary array, then release the action. */
int mm_template_set_cel(void *,void *body,MMI32 action,MMI32 cel,void *distance,const MMTemplateRuntimeServices *);
int mm_template_distance(void *,MMI32 action,MMI32 cel,MMI32 *,const MMTemplateRuntimeServices *);
int mm_template_cel_count(void *,MMI32 action,void *count,const MMTemplateRuntimeServices *);
int mm_template_action_flags(void *,MMI32 action,void *flags,const MMTemplateRuntimeServices *);
/* Sound existence clears before fetching. Fetch failure leaves tag untouched;
 * action GetSnd retains its native partial-output semantics. */
int mm_template_sound(void *,MMI32 action,MMI32 cel,void *exists,void *tag,const MMTemplateRuntimeServices *);
MMI32 mm_template_costume_count(void *,MMI32 set,const MMTemplateRuntimeServices *);
MMI32 mm_template_costume_id(void *,MMI32 set,MMI32 index,const MMTemplateRuntimeServices *);
int mm_template_is_accessory(void *,MMI32 set,const MMTemplateRuntimeServices *);
int mm_template_accessory_set(void *,MMI32 set,void *out,const MMTemplateRuntimeServices *);
int mm_template_same_accessory(MMI32,MMI32,const MMTemplateRuntimeServices *);
/* Graph-only helpers compare the dense child-ID prefix up to the first gap. */
int mm_template_has_models(const MMChunkAccess *,MMU32 type,MMU32 number);
int mm_template_equal_models(const MMChunkAccess *,MMU32,MMU32);
#ifdef __cplusplus
}
#endif
#endif
