/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#include "soc_core.h"
ASSERTNAME
RTCLASS(TagList)

PTagList TagList::PtaglNew(void)
{
    auto list = NewObj TagList();
    if (list && !list->_FInit())
        ReleasePpo(&list);
    return list;
}
bool TagList::_FInit(void)
{
    const auto allocator = CollectionAllocator();
    return three_mm_tag_list_init(&_list, &allocator) == THREE_MM_STORAGE_OK;
}
TagList::~TagList(void)
{
    AssertDo(three_mm_tag_list_dispose(&_list) == THREE_MM_STORAGE_OK, "invalid tag list disposal");
}
LegacyLong TagList::Ctag(void)
{
    AssertThis(0);
    return _list.count;
}
void TagList::GetTag(LegacyLong i, PTAG tag)
{
    AssertThis(0);
    ThreeMMTagCached value;
    AssertDo(three_mm_tag_list_get(&_list, i, &value) == THREE_MM_STORAGE_OK, "invalid tag index");
    TagFromCore(tag, value.tag);
}
bool TagList::FInsertTag(PTAG tag, bool children)
{
    AssertThis(0);
    auto value = TagToCore(*tag);
    return three_mm_tag_list_insert(&_list, &value, children) == THREE_MM_STORAGE_OK;
}
bool TagList::FInsertChild(PTAG tag, ChildChunkID slot, ChunkTagOrType type)
{
    AssertThis(0);
    auto value = TagToCore(*tag);
    const int result = three_mm_tag_list_child(&_list, &value, slot, type);
    if (result == THREE_MM_TAG_PARENT_NOT_FOUND)
        Bug("You should have inserted ptag first");
    return result == THREE_MM_STORAGE_OK;
}
bool TagList::FCacheTags(void)
{
    AssertThis(0);
    const ThreeMMTagListServices services = {
        nullptr,
        [](void *, const ThreeMMTag *value, int children) -> int {
            TAG tag;
            TagFromCore(&tag, *value);
            return vptagm->FCacheTagToHD(&tag, children != 0);
        },
        [](void *, const ThreeMMTag *value, ThreeMMU32 slot, ThreeMMU32 type, ThreeMMTag *out) -> int {
            TAG parent, child;
            TagFromCore(&parent, *value);
            if (!vptagm->FBuildChildTag(&parent, slot, type, &child))
                return 0;
            *out = TagToCore(child);
            return 1;
        }};
    return three_mm_tag_list_cache(&_list, &services) != 0;
}
#ifdef DEBUG
void TagList::AssertValid(LegacyUlong flags)
{
    TagList_PAR::AssertValid(fobjAllocated);
    Assert(_list.count <= _list.storage.buffers[1].capacity / 8, "invalid tag count");
}
void TagList::MarkMem(void)
{
    AssertThis(0);
    TagList_PAR::MarkMem();
    MarkPv(_list.storage.buffers[0].data);
    MarkPv(_list.storage.buffers[1].data);
}
#endif
