/* Host services for the shared block/handle policy. */
#ifndef KAUAI_HANDLE_SERVICES_H
#define KAUAI_HANDLE_SERVICES_H
#include "mm/block_owner.h"
const MMHandleServices *KauaiHandleServices();
const MMBlockFileServices *KauaiBlockFileServices();
const MMStorageAllocator *KauaiCopyAllocator();
#endif
