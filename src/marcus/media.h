#ifndef MARCUS_MEDIA_H
#define MARCUS_MEDIA_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusMediaOptions
{
    const char *iso, *doraemon_iso, *expansion_zip, *nick_iso, *nick_fixups, *frankie_zip;
} MarcusMediaOptions;

/* The physical paths are for legacy archive readers and their helper processes.
   The description names the original files for the application UI. */
int marcus_prepare_media(const MarcusMediaOptions *options, const char *workspace,
                         char *resources, size_t resources_size, char *content, size_t content_size,
                         char *description, size_t description_size, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
