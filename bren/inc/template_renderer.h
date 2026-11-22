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

void mm_brender_template_read_services(MMTemplateCreateServices *, MMResourceCache *);
int mm_brender_template_read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
MMTemplateOwner *mm_brender_template_fetch(MMResourceCache *, MMU32 type, MMU32 number, int *error);
int mm_brender_action_read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
int mm_brender_text_initialize(MMTemplateOwner *, MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *);
void mm_brender_text_generation_services(MMTextGenerationServices *);
MMTextNames *mm_brender_text_action_names(void);

#ifdef __cplusplus
}
#endif
#endif
