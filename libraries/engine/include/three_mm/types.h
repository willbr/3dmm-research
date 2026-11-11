#ifndef THREE_MM_ENGINE_TYPES_H
#define THREE_MM_ENGINE_TYPES_H
#include <limits.h>

#if CHAR_BIT != 8
#error Engine wire formats require eight-bit bytes
#endif
#if UINT_MAX == 0xffffffffUL && INT_MAX == 0x7fffffffL
typedef unsigned int ThreeMMU32;
typedef int ThreeMMI32;
#elif ULONG_MAX == 0xffffffffUL && LONG_MAX == 0x7fffffffL
typedef unsigned long ThreeMMU32;
typedef long ThreeMMI32;
#else
#error Engine requires a 32-bit C integer type
#endif
#endif
