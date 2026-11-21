/* Host services for the shared block/handle policy. */
#ifndef KAUAI_HANDLE_SERVICES_H
#define KAUAI_HANDLE_SERVICES_H
#include "mm/block_owner.h"
const MMHandleServices *KauaiHandleServices();
const MMBlockFileServices *KauaiBlockFileServices();
int KauaiDecodeBlockHandle(void *, void **handle);
const MMStorageAllocator *KauaiCopyAllocator();
const MMStorageAllocator *KauaiResourceObjectAllocator();
#endif
