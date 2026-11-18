/* Licensed under the MIT License. */
#ifndef MM_ENGINE_VXP_PACKAGE_H
#define MM_ENGINE_VXP_PACKAGE_H
#include "types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MMVxpResult
{
    MM_VXP_OK,
    MM_VXP_MEMORY,
    MM_VXP_MEMBER_COUNT,
    MM_VXP_MEMBER_RECORD,
    MM_VXP_NAME_LENGTH,
    MM_VXP_NAME_READ,
    MM_VXP_UNSAFE_MEMBER,
    MM_VXP_AMBIGUOUS_NAME,
    MM_VXP_BUDGET,
    MM_VXP_MULTIPLE_CONFIGS,
    MM_VXP_NO_CONFIG,
    MM_VXP_CONFIG_SIZE,
    MM_VXP_CONFIG_READ,
    MM_VXP_CONFIG_TEXT,
    MM_VXP_CONFIG_IDENTITY,
    MM_VXP_CONFIG_TYPE,
    MM_VXP_MOUNT_POINT,
    MM_VXP_REGID,
    MM_VXP_REGNAME,
    MM_VXP_NO_CONTENT,
    MM_VXP_NO_THUMBNAILS
} MMVxpResult;

/* ZIP file sizes and budgets are unsigned even on 32-bit hosts. */
typedef struct MMVxpSize { MMU32 low, high; } MMVxpSize;
typedef struct MMVxpLimits
{
    MMU32 members;
    MMVxpSize member_bytes, expanded_bytes;
} MMVxpLimits;

typedef struct MMVxpMemberInfo
{
    MMVxpSize bytes;
    size_t name_length;
    int supported;
} MMVxpMemberInfo;

typedef struct MMVxpReader
{
    void *context;
    MMU32 count;
    /* Supply the central-directory row. name_length excludes the terminator.
     * Report directories, encryption and unsupported methods via supported. */
    MMVxpResult (*member)(void *, MMU32, char *name, size_t capacity, MMVxpMemberInfo *);
    /* Decode exactly count bytes of the chosen CFG and check its CRC. */
    int (*configuration)(void *, MMU32 index, void *bytes, size_t count);
} MMVxpReader;

typedef enum MMVxpMemberKind
{
    MM_VXP_OTHER, MM_VXP_CONFIG, MM_VXP_CONTENT, MM_VXP_THUMBNAILS
} MMVxpMemberKind;

typedef struct MMVxpMember
{
    const char *name;
    MMVxpSize bytes;
    MMU32 index; /* Original central-directory slot. */
    MMVxpMemberKind kind;
} MMVxpMember;

typedef struct MMVxpField { const char *key, *value; } MMVxpField;
typedef struct MMVxpPackage
{
    MMVxpMember *members;
    MMVxpField *fields;
    size_t member_count, field_count;
    const char *name, *author, *short_name;
    char *configuration;
} MMVxpPackage;

/* The result owns all storage on success. Pass an empty result. Members retain
 * original directory order and the exact PhysicsFS duplicate winners. Fields
 * use ASCII-folded keys and retain the last value for each key. Failure leaves
 * result untouched. failed_member is optional and receives a slot for unsafe
 * names and casing conflicts. */
MMVxpResult mm_vxp_read_package(const MMVxpReader *, const MMVxpLimits *, MMVxpPackage *, MMU32 *failed_member);
void mm_vxp_dispose(MMVxpPackage *);
const char *mm_vxp_error(MMVxpResult);
/* Returns a materialized extension for the two chunky content kinds. */
const char *mm_vxp_content_extension(MMVxpMemberKind);
void mm_vxp_fold_ascii(char *text, size_t length);

#ifdef __cplusplus
}
#endif
#endif
