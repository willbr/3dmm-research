/* Host services for the shared block/handle policy. */
#ifndef KAUAI_HANDLE_SERVICES_H
#define KAUAI_HANDLE_SERVICES_H
#include "three_mm/block_io.h"
const ThreeMMHandleServices *KauaiHandleServices();
const ThreeMMStorageAllocator *KauaiCopyAllocator();
#endif
