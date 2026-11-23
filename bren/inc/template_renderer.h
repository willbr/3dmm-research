#ifndef MM_BRENDER_TEMPLATE_RENDERER_H
#define MM_BRENDER_TEMPLATE_RENDERER_H

#include "mm/text_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native allocation, archive names, decoding and diagnostics. */
const MMTemplateOwnerHost *mm_kauai_template_host(void);
const MMActionResourceHost *mm_kauai_action_host(void);
void mm_kauai_template_read_services(MMTemplateCreateServices *);
void mm_kauai_text_read_services(MMTextResourceServices *);
void mm_kauai_template_validate(MMTemplateOwner *);
void mm_kauai_template_mark(MMTemplateOwner *);
void mm_kauai_font_check_index(MMI32 index, MMI32 count);
void mm_kauai_text_action_name_missing(void *);

void mm_brender_template_read_services(MMTemplateCreateServices *, MMResourceCache *);
int mm_brender_template_read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
MMTemplateOwner *mm_brender_template_fetch(MMResourceCache *, MMU32 type, MMU32 number, int *error);
int mm_brender_action_read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
int mm_brender_text_initialize(MMTemplateOwner *, MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *);
void mm_brender_text_generation_services(MMTextGenerationServices *);
MMTemplateOwner *mm_brender_text_create(const MMStringValue *, MMI32 shape, const MMTag *,
                                        const MMTemplateFontProvider *);
MMTemplateOwner *mm_brender_text_duplicate(MMTemplateOwner *);
int mm_brender_text_change(MMTemplateOwner *, const MMStringValue *, MMI32 shape, const MMTag *);
int mm_brender_text_action_names_read(const MMReadStream *, int encoding, MMStringConvert, void *context);
int mm_brender_text_action_names_load(MMResourceCache *, MMU32 number, int encoding, MMStringConvert, void *context);
void mm_brender_text_action_names_clear(void);

struct MMBodyOwner;
struct MMMaterialOwner;
struct MMBodyOwner *mm_brender_template_body(MMTemplateOwner *);
int mm_brender_template_default_costume(MMTemplateOwner *, struct MMBodyOwner *);
struct MMMaterialOwner *mm_brender_template_costume(MMTemplateOwner *, MMI32 id);
MMI32 mm_brender_template_costume_count(MMTemplateOwner *, MMI32 set);
MMI32 mm_brender_template_costume_id(MMTemplateOwner *, MMI32 set, MMI32 index);
int mm_brender_template_is_accessory(MMTemplateOwner *, MMI32 set);
int mm_brender_template_accessory_set(MMTemplateOwner *, MMI32 set, void *out);
int mm_brender_template_same_accessory(MMTemplateOwner *, MMI32 first, MMI32 second);
void mm_brender_template_rest(MMTemplateOwner *, unsigned short *x, unsigned short *y, unsigned short *z);
void mm_brender_template_rest_pose(MMTemplateOwner *, struct MMBodyOwner *);
int mm_brender_template_action_name(MMTemplateOwner *, MMI32 action, const MMStringValue *);
/* Opaque scalar outputs use the C engine's four-byte fixed-point layout. */
int mm_brender_template_set_cel(MMTemplateOwner *, struct MMBodyOwner *, MMI32 action, MMI32 cel, void *distance);
int mm_brender_template_distance(MMTemplateOwner *, MMI32 action, MMI32 cel, void *distance);
int mm_brender_template_cel_count(MMTemplateOwner *, MMI32 action, void *count);
int mm_brender_template_action_flags(MMTemplateOwner *, MMI32 action, void *flags);
int mm_brender_template_sound(MMTemplateOwner *, MMI32 action, MMI32 cel, int *exists, MMTag *);
int mm_brender_text_adjust_body(MMTemplateOwner *, struct MMBodyOwner *);
struct MMActorEffectRenderer;
void mm_brender_actor_effect_renderer(struct MMActorEffectRenderer *);

#ifdef __cplusplus
}
#endif
#endif
