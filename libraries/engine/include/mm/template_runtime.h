/* Template action/accessory operations converted from engine tmpl.cpp/mtrl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TEMPLATE_RUNTIME_H
#define MM_ENGINE_TEMPLATE_RUNTIME_H
#include "template_create.h"
#ifdef __cplusplus
extern "C" {
#endif
enum MMTemplateResourceKind { MM_TEMPLATE_ACTION, MM_TEMPLATE_MODEL, MM_TEMPLATE_CUSTOM };
enum { MM_TEMPLATE_BMDL=0x424d444c };
enum { MM_TEMPLATE_KEEP_MODEL = -1 };
typedef struct MMTemplateCelServices
{
    void *context;
    void (*part)(void *, MMU32 part, MMI32 *model, MMI32 *matrix);
    int (*prepare_part)(void *, MMU32 part, MMI32 model, MMI32 matrix, int replace_model);
    void (*begin_apply)(void *);
    void (*apply_part)(void *, MMU32 part, int replace_model, MMI32 matrix);
} MMTemplateCelServices;
/* The cel count must be positive. */
MMI32 mm_template_wrap_cel(MMI32 cel, MMI32 count);
/* Read a stable cel twice. Prepare all replacement models in part order
 * before beginning publication. In the second pass a keep-model directive
 * still applies its matrix. The caller owns staged models. begin_apply is
 * optional. Only prepare_part may fail. Returns one on success. */
int mm_template_bind_cel(MMU32 part_count, const MMTemplateCelServices *);
typedef struct MMTemplateBinding { void *file,*cache; MMU32 type,number; } MMTemplateBinding;
typedef struct MMTemplateRuntimeServices
{
    MMTemplateCreateServices storage;
    void (*binding)(void *,void *object,MMTemplateBinding *);
    void *(*fetch)(void *,void *cache,MMU32,MMU32,int kind);
    int (*name)(void *,void *file,MMU32,MMU32,void *out);
    void *(*action)(void *,void *object,MMI32 id);
    void *(*model)(void *,void *object,MMU32 id);
    MMI32 (*cel_count)(void *,void *action);
    MMI32 (*distance)(void *,void *action,MMI32 cel);
    MMU32 (*flags)(void *,void *action);
    void (*part)(void *,void *action,MMI32 cel,MMI32 part,MMI32 *model,MMI32 *matrix);
    void (*matrix)(void *,void *action,MMI32 matrix,MMI32 out[12]);
    void (*set_model)(void *,void *body,MMI32 part,void *model);
    void (*set_matrix)(void *,void *body,MMI32 part,const MMI32 matrix[12]);
    int (*sound)(void *,void *action,MMI32 cel,void *tag);
    void (*sound_exists)(void *,void *out,int exists);
} MMTemplateRuntimeServices;
/* Actions have positive cel counts. Part counts fit the native signed-short
 * loop range; parent/set/matrix indices and costume indices are valid. Scalar
 * and matrix elements are signed 32-bit fixed-point representations. Callbacks
 * do not reenter or mutate unrelated state. action/model preserve virtual
 * fetches, including ThreeDText. Resources returned by fetch are retained. */
/* Opaque scalar output pointers hold exactly four bytes; representation copies
 * bridge native Windows long/unsigned long and C int/unsigned int storage
 * without incompatible typed pointer aliasing. */
void *mm_template_fetch_child(void *,MMU32 id,int kind,const MMTemplateRuntimeServices *);
int mm_template_action_name(void *,MMI32 id,void *out,const MMTemplateRuntimeServices *);
/* Fetch every model before publishing distance or modifying the body. A nil
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
int mm_template_same_accessory(void *,MMI32,MMI32,const MMTemplateRuntimeServices *);
/* Graph-only helpers retain dense child-ID prefix semantics: a gap ends the
 * comparison, and children beyond that gap are not inspected. */
int mm_template_has_models(void *file,MMU32 type,MMU32 number,const MMTemplateCreateServices *);
int mm_template_equal_models(void *file,MMU32,MMU32,const MMTemplateCreateServices *);
#ifdef __cplusplus
}
#endif
#endif
