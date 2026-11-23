/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_PORTFOLIO_H
#define MM_ENGINE_PORTFOLIO_H

#include "masked_bitmap.h"
#include "resource_cache.h"
#include "string_value.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MM_PORTFOLIO_PREVIEW_MOVIE = 1, MM_PORTFOLIO_PREVIEW_BITMAP = 4 };

typedef struct MMPortfolioPreviewServices
{
    MMResourceCache *(*open_movie)(void *selection);
    MMResourceBlockReader thumbnail_reader;
    MMMaskedOwner *(*import_bitmap)(void *selection);
} MMPortfolioPreviewServices;

/* The selection and services are borrowed. A successful open supplies one
 * owned cache reference. Prefer the first scene's thumbnail, then the bitmap
 * import if requested. The caller owns the returned bitmap. */
MMMaskedOwner *mm_portfolio_preview(void *selection, MMU32 previews, const MMPortfolioPreviewServices *);

/* The extension contains count native character units, without a leading
 * dot or terminator. Failure leaves the path intact. */
int mm_portfolio_append_extension(const MMStringValue *path, const void *extension, MMI32 count);
void mm_portfolio_remove_extension(const MMStringValue *leaf);

#ifdef __cplusplus
}
#endif
#endif
