/* Host services for the shared block/handle policy. */
#ifndef KAUAI_HANDLE_SERVICES_H
#define KAUAI_HANDLE_SERVICES_H
#include "mm/block_io.h"
const MMHandleServices *KauaiHandleServices();
const MMStorageAllocator *KauaiCopyAllocator();
#endif
