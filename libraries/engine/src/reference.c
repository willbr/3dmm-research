/* Reference counting converted from Kauai base.cpp and file.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/reference.h"
#include "mm/wide.h"
void mm_reference_init(MMReference *state)
{ state->count=1; }
MMI32 mm_reference_add(MMReference *state)
{ state->count=mm_signed_word((MMU32)state->count+1U); return state->count; }
MMI32 mm_reference_drop(MMReference *state)
{ state->count=mm_signed_word((MMU32)state->count-1U); return state->count; }
void mm_reference_release(MMReference *state,void *object,void (*destroy)(void *))
{ if (mm_reference_drop(state)<=0) destroy(object); }
void mm_reference_release_marked(MMReference *state,int marked,void *object,
    void (*issue)(void *),void (*destroy)(void *))
{
    if (state->count<=0) { issue(object); state->count=0; return; }
    if (mm_reference_drop(state)==0 && !marked) destroy(object);
}
