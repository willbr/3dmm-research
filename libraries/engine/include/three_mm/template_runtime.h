/* Template action/accessory operations converted from engine tmpl.cpp/mtrl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_TEMPLATE_RUNTIME_H
#define THREE_MM_ENGINE_TEMPLATE_RUNTIME_H
#include "template_create.h"
#ifdef __cplusplus
extern "C" {
#endif
enum ThreeMMTemplateResourceKind { THREE_MM_TEMPLATE_ACTION, THREE_MM_TEMPLATE_MODEL, THREE_MM_TEMPLATE_CUSTOM };
enum { THREE_MM_TEMPLATE_BMDL=0x424d444c };
typedef struct ThreeMMTemplateBinding { void *file,*cache; ThreeMMU32 type,number; } ThreeMMTemplateBinding;
typedef struct ThreeMMTemplateRuntimeServices
{
    ThreeMMTemplateCreateServices storage;
    void (*binding)(void *,void *object,ThreeMMTemplateBinding *);
    void *(*fetch)(void *,void *cache,ThreeMMU32,ThreeMMU32,int kind);
    int (*name)(void *,void *file,ThreeMMU32,ThreeMMU32,void *out);
    void *(*action)(void *,void *object,ThreeMMI32 id);
    void *(*model)(void *,void *object,ThreeMMU32 id);
    ThreeMMI32 (*cel_count)(void *,void *action);
    ThreeMMI32 (*distance)(void *,void *action,ThreeMMI32 cel);
    ThreeMMU32 (*flags)(void *,void *action);
    void (*part)(void *,void *action,ThreeMMI32 cel,ThreeMMI32 part,ThreeMMI32 *model,ThreeMMI32 *matrix);
    void (*matrix)(void *,void *action,ThreeMMI32 matrix,ThreeMMI32 out[12]);
    void (*set_model)(void *,void *body,ThreeMMI32 part,void *model);
    void (*set_matrix)(void *,void *body,ThreeMMI32 part,const ThreeMMI32 matrix[12]);
    int (*sound)(void *,void *action,ThreeMMI32 cel,void *tag);
    void (*sound_exists)(void *,void *out,int exists);
} ThreeMMTemplateRuntimeServices;
/* Actions have positive cel counts. Part counts fit the native signed-short
 * loop range; parent/set/matrix indices and costume indices are valid. Scalar
 * and matrix elements are signed 32-bit fixed-point representations. Callbacks
 * do not reenter or mutate unrelated state. action/model preserve virtual
 * fetches, including ThreeDText. Resources returned by fetch are retained. */
/* Opaque scalar output pointers hold exactly four bytes; representation copies
 * bridge native Windows long/unsigned long and C int/unsigned int storage
 * without incompatible typed pointer aliasing. */
void *three_mm_template_fetch_child(void *,ThreeMMU32 id,int kind,const ThreeMMTemplateRuntimeServices *);
int three_mm_template_action_name(void *,ThreeMMI32 id,void *out,const ThreeMMTemplateRuntimeServices *);
/* Fetch every model before publishing distance or modifying the body. A nil
 * model ID preserves the accessory model but still applies its cel matrix.
 * Failure leaves body/distance unchanged. Release/clear models in part order,
 * free the temporary array, then release the action. */
int three_mm_template_set_cel(void *,void *body,ThreeMMI32 action,ThreeMMI32 cel,void *distance,const ThreeMMTemplateRuntimeServices *);
int three_mm_template_distance(void *,ThreeMMI32 action,ThreeMMI32 cel,ThreeMMI32 *,const ThreeMMTemplateRuntimeServices *);
int three_mm_template_cel_count(void *,ThreeMMI32 action,void *count,const ThreeMMTemplateRuntimeServices *);
int three_mm_template_action_flags(void *,ThreeMMI32 action,void *flags,const ThreeMMTemplateRuntimeServices *);
/* Sound existence clears before fetching. Fetch failure leaves tag untouched;
 * action GetSnd retains its native partial-output semantics. */
int three_mm_template_sound(void *,ThreeMMI32 action,ThreeMMI32 cel,void *exists,void *tag,const ThreeMMTemplateRuntimeServices *);
ThreeMMI32 three_mm_template_costume_count(void *,ThreeMMI32 set,const ThreeMMTemplateRuntimeServices *);
ThreeMMI32 three_mm_template_costume_id(void *,ThreeMMI32 set,ThreeMMI32 index,const ThreeMMTemplateRuntimeServices *);
int three_mm_template_is_accessory(void *,ThreeMMI32 set,const ThreeMMTemplateRuntimeServices *);
int three_mm_template_accessory_set(void *,ThreeMMI32 set,void *out,const ThreeMMTemplateRuntimeServices *);
int three_mm_template_same_accessory(void *,ThreeMMI32,ThreeMMI32,const ThreeMMTemplateRuntimeServices *);
/* Graph-only helpers retain dense child-ID prefix semantics: a gap ends the
 * comparison, and children beyond that gap are not inspected. */
int three_mm_template_has_models(void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMTemplateCreateServices *);
int three_mm_template_equal_models(void *file,ThreeMMU32,ThreeMMU32,const ThreeMMTemplateCreateServices *);
#ifdef __cplusplus
}
#endif
#endif
