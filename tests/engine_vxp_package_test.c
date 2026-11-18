/* Licensed under the MIT License. */
#include "mm/vxp_package.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char configuration[] =
    "# comment\r\n ; another comment\n"
    "  NAME \t = First \r\n nAmE= Caf\xe9\r\n"
    "Author = Author\n Type = pOrTaBlE\r\n"
    " Mount Point = V3DMM \nRegID=6\n RegName = Virtual3DMM/V3DMM\n"
    " Unknown = original=value\n malformed line\n = empty key";
static const char wrong_type[] = "Name=Example\nAuthor=Author\nType=Library";
static const char wrong_override[] = "Name=Example\nAuthor=Author\nType=Portable\nRegID=27";

typedef struct Fixture
{
    const char *names[5];
    const char *configuration;
    size_t configuration_length;
    MMVxpSize readme_bytes;
    int bad_crc;
    MMU32 chosen_config;
} Fixture;

static MMVxpResult member(void *context, MMU32 index, char *name, size_t capacity, MMVxpMemberInfo *info)
{
    Fixture *fixture = (Fixture *)context;
    const char *source = fixture->names[index];
    info->name_length = strlen(source);
    assert(info->name_length < capacity);
    memcpy(name, source, info->name_length);
    info->supported = 1;
    info->bytes.high = 0;
    info->bytes.low = 12;
    if (index == 0 || index == 4) info->bytes.low = (MMU32)fixture->configuration_length;
    if (index == 2) info->bytes = fixture->readme_bytes;
    return MM_VXP_OK;
}

static int read_configuration(void *context, MMU32 index, void *bytes, size_t count)
{
    Fixture *fixture = (Fixture *)context;
    fixture->chosen_config = index;
    assert(count == fixture->configuration_length);
    if (fixture->bad_crc) return 0;
    memcpy(bytes, fixture->configuration, count);
    return 1;
}

static const char *field(const MMVxpPackage *package, const char *key)
{
    size_t i;
    for (i = 0; i < package->field_count; ++i)
        if (!strcmp(package->fields[i].key, key)) return package->fields[i].value;
    return NULL;
}

int main(void)
{
    Fixture fixture = {
        {"ScEnE.CFG", "scene.3CN", "notes.txt", "scene.3Th", "ScEnE.CFG"},
        configuration, sizeof(configuration) - 1, {64, 1}, 0, 5
    };
    MMVxpReader reader = {NULL, 5, member, read_configuration};
    MMVxpLimits limits = {5, {64, 1}, {4096, 1}};
    MMVxpPackage package = {0};
    MMU32 failed;
    char invalid_text[sizeof(configuration)];
    reader.context = &fixture;
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_OK);
    assert(fixture.chosen_config == 0 && package.member_count == 4 && package.field_count == 8);
    assert(!strcmp(package.name, "Caf\xe9") && !strcmp(package.author, "Author"));
    assert(!strcmp(package.short_name, "ScEnE"));
    assert(!strcmp(field(&package, "unknown"), "original=value"));
    assert(!strcmp(field(&package, ""), "empty key"));
    assert(package.members[1].index == 1 && package.members[2].index == 2 && package.members[3].index == 3);
    assert(package.members[2].bytes.high == 1 && package.members[2].bytes.low == 64);
    assert(!strcmp(mm_vxp_content_extension(package.members[1].kind), ".3cn"));
    assert(!strcmp(mm_vxp_content_extension(package.members[3].kind), ".3th"));
    mm_vxp_dispose(&package);

    limits.expanded_bytes.low = 100;
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_BUDGET);
    limits.expanded_bytes.low = 4096;
    fixture.names[4] = "scene.cfg";
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_AMBIGUOUS_NAME && failed == 4);
    fixture.names[4] = "folder/notes.txt";
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_UNSAFE_MEMBER && failed == 4);
    fixture.names[4] = "other.cfg";
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_MULTIPLE_CONFIGS);
    fixture.names[4] = "ScEnE.CFG";
    fixture.names[3] = "other.3th";
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_NO_THUMBNAILS);
    fixture.names[3] = "scene.3Th";
    fixture.bad_crc = 1;
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_CONFIG_READ);
    fixture.bad_crc = 0;
    memcpy(invalid_text, configuration, sizeof(configuration));
    invalid_text[3] = '\0';
    fixture.configuration = invalid_text;
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_CONFIG_TEXT);
    fixture.configuration = wrong_type;
    fixture.configuration_length = sizeof(wrong_type) - 1;
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_CONFIG_TYPE);
    fixture.configuration = wrong_override;
    fixture.configuration_length = sizeof(wrong_override) - 1;
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_REGID);
    limits.member_bytes.high = limits.expanded_bytes.high = (MMU32)0xffffffffUL;
    limits.member_bytes.low = limits.expanded_bytes.low = (MMU32)0xffffffffUL;
    fixture.readme_bytes = limits.member_bytes;
    assert(mm_vxp_read_package(&reader, &limits, &package, &failed) == MM_VXP_BUDGET);
    assert(!package.members && !package.configuration && !package.fields);
    puts("VXP metadata, directory order, wide budgets, CRC handling and rejection passed.");
    return 0;
}
