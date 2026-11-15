#ifndef MM_ENGINE_TYPES_H
#define MM_ENGINE_TYPES_H
#include <limits.h>

#if CHAR_BIT != 8
#error Engine wire formats require eight-bit bytes
#endif
#if UINT_MAX == 0xffffffffUL && INT_MAX == 0x7fffffffL
typedef unsigned int MMU32;
typedef int MMI32;
#elif ULONG_MAX == 0xffffffffUL && LONG_MAX == 0x7fffffffL
typedef unsigned long MMU32;
typedef long MMI32;
#else
#error Engine requires a 32-bit C integer type
#endif
#endif
