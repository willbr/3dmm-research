/*
 * Copyright (c) 1992,1993-1995 by Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: g386ifix.h 1.6 1995/02/22 21:37:10 sam Exp $
 * $Locker:  $
 *
 * Inline fixed point maths for GNU-C (386)
 */

/*
 * Old version using inline functions
 */
#if 0
__inline static const br_int_32 IFixedMul(br_int_32 a, br_int_32 b)
{
	register br_int_32 __r;

	__asm (
		 "imull %2\n"
		 "shrdl $16,%%edx,%%eax\n"
	: "=a" (__r)
	: "0" (a), "rm" (b)
	: "eax", "edx", "cc");

	return __r;
}

__inline static const br_int_32 IFixedDiv(br_int_32 a, br_int_32 b)
{
	register br_int_32 __r;

	__asm (
		"movl	%%eax,%%edx\n" 
		"shll	$16,%%eax\n"
		"sarl	$16,%%edx\n"
		"idivl	%2\n"
	: "=a" (__r)				/* outputs 	*/
	: "0" (a), "rm" (b)			/* inputs 	*/
	: "eax", "edx", "cc"		/* uses 	*/
	);

	return __r;
}

__inline static const br_int_32 IFixedDivR(br_int_32 a, br_int_32 b)
{
	register br_int_32 __r;

	__asm (
		"cdq\n"
		"xchg %%edx,%%eax\n"
		"shrdl $16,%%edx,%%eax\n"
		"sarl $16,%%edx\n"
		"idivl %2\n"
	: "=a" (__r)
	: "0" (a), "rm" (b)
	: "eax", "edx", "cc");

	return __r;
}

__inline static const br_int_32 IFixedDivF(br_int_32 a, br_int_32 b)
{
	register br_int_32 __r;

	__asm (
		"xorl	%%eax,%%eax\n"
		"sarl	$1,%%edx\n"
		"rcrl	$1,%%eax\n"
		"divl	%2\n"
	: "=a" (__r)
	: "d" (a), "rm" (b)
	: "eax", "edx", "cc");

	return __r;
}

__inline static const br_int_32 IFixedRcp(br_int_32 a)
{
	register br_int_32 __r;

	__asm (
		"movl	$1,%%edx\n"
		"xorl	%%eax,%%eax\n"
		"idivl	%1\n"
	: "=a" (__r)
	: "rm" (a)
	: "eax", "edx", "cc");

	return __r;
}

__inline static const br_int_32 IFixedMulDiv(br_int_32 a, br_int_32 b, br_int_32 c)
{
	register br_int_32 __r;

	__asm (
		"imull %2\n"
		"idivl %3\n"
	: "=a" (__r)
	: "0" (a), "rm" (b), "rm" (c)
	: "eax", "edx", "cc");

	return __r;
}

__inline static const br_int_32 IFixedMac2(
	br_int_32 a, br_int_32 b,
	br_int_32 c, br_int_32 d)
{
	register br_int_32 __r;

	__asm (
		 "imull %2\n"
		 "movl  %%eax,%%ebx\n"
		 "movl  %%edx,%%ecx\n"

		 "movl  %3,%%eax\n"
		 "imull %4\n"
		 "addl	%%ebx,%%eax\n"
		 "adcl	%%ecx,%%edx\n"
		 "shrdl $16,%%edx,%%eax\n"
	: "=a" (__r)
	: "0" (a), "rm" (b), "rm" (c), "rm" (d)
	: "eax", "ebx", "ecx", "edx", "cc");

	return __r;
}

__inline static const br_int_32 IFixedMac3(
	br_int_32 a, br_int_32 b,
	br_int_32 c, br_int_32 d,
	br_int_32 e, br_int_32 f)
{
	register br_int_32 __r;

	__asm (
		 "imull %2\n"
		 "movl  %%eax,%%ebx\n"
		 "movl  %%edx,%%ecx\n"

		 "movl  %3,%%eax\n"
		 "imull %4\n"
		 "addl	%%eax,%%ebx\n"
		 "adcl	%%edx,%%ecx\n"

		 "movl  %5,%%eax\n"
		 "imull %6\n"
		 "addl	%%ebx,%%eax\n"
		 "adcl	%%ecx,%%edx\n"

		 "shrdl $16,%%edx,%%eax\n"
	: "=a" (__r)
	:  "0" (a), "rm" (b), "rm" (c), "rm" (d), "rm" (e), "rm" (f)
	: "eax", "ebx", "ecx", "edx", "cc");

	return __r;
}

__inline static const br_int_32 IFixedMac4(
	br_int_32 a, br_int_32 b,
	br_int_32 c, br_int_32 d,
	br_int_32 e, br_int_32 f,
	br_int_32 g, br_int_32 h)
{
	register br_int_32 __r;

	__asm (
		 "imull %2\n"
		 "movl  %%eax,%%ebx\n"
		 "movl  %%edx,%%ecx\n"

		 "movl  %3,%%eax\n"
		 "imull %4\n"
		 "addl	%%eax,%%ebx\n"
		 "adcl	%%edx,%%ecx\n"

		 "movl  %5,%%eax\n"
		 "imull %6\n"
		 "addl	%%eax,%%ebx\n"
		 "adcl	%%edx,%%ecx\n"

		 "movl  %7,%%eax\n"
		 "imull %8\n"
		 "addl	%%ebx,%%eax\n"
		 "adcl	%%ecx,%%edx\n"

		 "shrdl $16,%%edx,%%eax\n"
	: "=a" (__r)
	: "0" (a), "rm" (b), "rm" (c), "rm" (d), "rm" (e), "rm" (f), "rm" (g), "rm" (h)
	: "eax", "ebx", "ecx", "edx", "cc");

	return __r;
}

__inline static const br_int_32 IFixedSqr(br_int_32 a)
{
	__asm (
		 "imull %%eax\n"
		 "shrdl $16,%%edx,%%eax\n"
	: "=a" (a)
	: "0" (a)
	: "eax", "edx", "cc");

	return a;
}

__inline static const br_int_32 IFixedSqr2(br_int_32 a, br_int_32 b)
{
	register br_int_32 __r;

	__asm (
		 "imull %%eax\n"
		 "movl  %%eax,%%ebx\n"
		 "movl  %%edx,%%ecx\n"

		 "movl  %2,%%eax\n"
		 "imull %%eax\n"
		 "addl	%%ebx,%%eax\n"
		 "adcl	%%ecx,%%edx\n"
		 "shrdl $16,%%edx,%%eax\n"
	: "=a" (__r)
	: "0" (a), "rm" (b)
	: "eax", "ebx", "ecx", "edx", "cc");

	return __r;
}

__inline static const br_int_32 IFixedSqr3(
	br_int_32 a, br_int_32 b,br_int_32 c)
{
	register br_int_32 __r;

	__asm (
		 "imull %%eax\n"
		 "movl  %%eax,%%ebx\n"
		 "movl  %%edx,%%ecx\n"

		 "movl  %2,%%eax\n"
		 "imull %%eax\n"
		 "addl	%%eax,%%ebx\n"
		 "adcl	%%edx,%%ecx\n"

		 "movl  %3,%%eax\n"
		 "imull %%eax\n"
		 "addl	%%ebx,%%eax\n"
		 "adcl	%%ecx,%%edx\n"

		 "shrdl $16,%%edx,%%eax\n"
	: "=a" (__r)
	: "0" (a), "rm" (b), "rm" (c)
	: "eax", "ebx", "ecx", "edx", "cc");

	return __r;
}

__inline static const br_int_32 IFixedSqr4(
	br_int_32 a, br_int_32 b,
	br_int_32 c, br_int_32 d)
{
	register br_int_32 __r;

	__asm (
		 "imull %%eax\n"
		 "movl  %%eax,%%ebx\n"
		 "movl  %%edx,%%ecx\n"

		 "movl  %2,%%eax\n"
		 "imull %%eax\n"
		 "addl	%%eax,%%ebx\n"
		 "adcl	%%edx,%%ecx\n"

		 "movl  %3,%%eax\n"
		 "imull %%eax\n"
		 "addl	%%eax,%%ebx\n"
		 "adcl	%%edx,%%ecx\n"

		 "movl  %4,%%eax\n"
		 "imull %%eax\n"
		 "addl	%%ebx,%%eax\n"
		 "adcl	%%ecx,%%edx\n"

		 "shrdl $16,%%edx,%%eax\n"
	: "=a" (__r)
	: "0" (a), "rm" (b), "rm" (c), "rm" (d)
	: "eax", "ebx", "ecx", "edx", "cc");

	return __r;
}

#define BrFixedMul(a, b) IFixedMul(a, b)
#define BrFixedMac2(a, b, c, d) IFixedMac2(a, b, c, d)
#define BrFixedMac3(a, b, c, d, e, f) IFixedMac3(a, b, c, d, e, f)
#define BrFixedMac4(a, b, c, d, e, f, g, h) IFixedMac4(a, b, c, d, e, f, g, h)

#define BrFixedDiv(a, b) IFixedDiv(a, b)
#define BrFixedDivR(a, b) IFixedDivR(a, b)
#define BrFixedDivF(a, b) IFixedDivF(a, b)
#define BrFixedMulDiv(a, b, c) IFixedMulDiv(a, b, c)
#define BrFixedRcp(a) IFixedRcp(a)

#define BrFixedSqr(a) IFixedSqr(a)
#define BrFixedSqr2(a, b) IFixedSqr2(a, b)
#define BrFixedSqr3(a, b, c) IFixedSqr3(a, b, c)
#define BrFixedSqr4(a, b, c, d) IFixedSqr4(a, b, c, d)
#endif

/*
 * New version using direct expansion
 */
#if 0
#define BrFixedMul(__a, __b)                                                                                           \
    ({                                                                                                                 \
        br_int_32 __r;                                                                                                      \
                                                                                                                       \
        __asm("		imull %2\n"                                                                                           \
              "		shrdl $16,%%edx,%%eax\n"                                                                              \
              : "=a"(__r)                                                                                              \
              : "0"((br_int_32)(__a)), "rm"((br_int_32)(__b))                                                                    \
              : "edx", "cc");                                                                                          \
                                                                                                                       \
        __r;                                                                                                           \
    })

#define BrFixedDiv(__a, __b)                                                                                           \
    ({                                                                                                                 \
        register br_int_32 __r;                                                                                             \
                                                                                                                       \
        __asm("		movl	%%eax,%%edx\n"                                                                                   \
              "		shll	$16,%%eax\n"                                                                                     \
              "		sarl	$16,%%edx\n"                                                                                     \
              "		idivl	%2\n"                                                                                           \
              : "=a"(__r)                                                                                              \
              : "a"((br_int_32)(__a)), "rm"((br_int_32)(__b))                                                                    \
              : "edx", "cc");                                                                                          \
                                                                                                                       \
        __r;                                                                                                           \
    })

#define BrFixedMulDiv(__a, __b, __c)                                                                                   \
    ({                                                                                                                 \
        register br_int_32 __r;                                                                                             \
                                                                                                                       \
        __asm("imull %2\n"                                                                                             \
              "idivl %3\n"                                                                                             \
              : "=a"(__r)                                                                                              \
              : "0"((br_int_32)(__a)), "rm"((br_int_32)(__b)), "rm"((br_int_32)(__c))                                                 \
              : "edx", "cc");                                                                                          \
                                                                                                                       \
        __r;                                                                                                           \
    })

#define BrFixedMac2(__a, __b, __c, __d)                                                                                \
    ({                                                                                                                 \
        register br_int_32 __r;                                                                                             \
                                                                                                                       \
        __asm("imull %2\n"                                                                                             \
              "movl  %%eax,%%ebx\n"                                                                                    \
              "movl  %%edx,%%ecx\n"                                                                                    \
                                                                                                                       \
              "movl  %3,%%eax\n"                                                                                       \
              "imull %4\n"                                                                                             \
              "addl	%%ebx,%%eax\n"                                                                                     \
              "adcl	%%ecx,%%edx\n"                                                                                     \
              "shrdl $16,%%edx,%%eax\n"                                                                                \
              : "=a"(__r)                                                                                              \
              : "0"((br_int_32)(__a)), "rm"((br_int_32)(__b)), "rm"((br_int_32)(__c)), "rm"((br_int_32)(__d))                              \
              : "ebx", "ecx", "edx", "cc");                                                                            \
                                                                                                                       \
        __r;                                                                                                           \
    })

#define BrFixedMac3(__a, __b, __c, __d, __e, __f)                                                                      \
    ({                                                                                                                 \
        register br_int_32 __r;                                                                                             \
                                                                                                                       \
        __asm("imull %2\n"                                                                                             \
              "movl  %%eax,%%ebx\n"                                                                                    \
              "movl  %%edx,%%ecx\n"                                                                                    \
                                                                                                                       \
              "movl  %3,%%eax\n"                                                                                       \
              "imull %4\n"                                                                                             \
              "addl	%%eax,%%ebx\n"                                                                                     \
              "adcl	%%edx,%%ecx\n"                                                                                     \
                                                                                                                       \
              "movl  %5,%%eax\n"                                                                                       \
              "imull %6\n"                                                                                             \
              "addl	%%ebx,%%eax\n"                                                                                     \
              "adcl	%%ecx,%%edx\n"                                                                                     \
                                                                                                                       \
              "shrdl $16,%%edx,%%eax\n"                                                                                \
              : "=a"(__r)                                                                                              \
              : "0"((br_int_32)(__a)), "rm"((br_int_32)(__b)), "rm"((br_int_32)(__c)), "rm"((br_int_32)(__d)), "rm"((br_int_32)(__e)),          \
                "rm"((br_int_32)(__f))                                                                                      \
              : "ebx", "ecx", "edx", "cc");                                                                            \
                                                                                                                       \
        __r;                                                                                                           \
    })

#define BrFixedMac4(__a, __b, __c, __d, __e, __f, __g, __h)                                                            \
    ({                                                                                                                 \
        register br_int_32 __r;                                                                                             \
                                                                                                                       \
        __asm("imull %2\n"                                                                                             \
              "movl  %%eax,%%ebx\n"                                                                                    \
              "movl  %%edx,%%ecx\n"                                                                                    \
                                                                                                                       \
              "movl  %3,%%eax\n"                                                                                       \
              "imull %4\n"                                                                                             \
              "addl	%%eax,%%ebx\n"                                                                                     \
              "adcl	%%edx,%%ecx\n"                                                                                     \
                                                                                                                       \
              "movl  %5,%%eax\n"                                                                                       \
              "imull %6\n"                                                                                             \
              "addl	%%eax,%%ebx\n"                                                                                     \
              "adcl	%%edx,%%ecx\n"                                                                                     \
                                                                                                                       \
              "movl  %7,%%eax\n"                                                                                       \
              "imull %8\n"                                                                                             \
              "addl	%%ebx,%%eax\n"                                                                                     \
              "adcl	%%ecx,%%edx\n"                                                                                     \
                                                                                                                       \
              "shrdl $16,%%edx,%%eax\n"                                                                                \
              : "=a"(__r)                                                                                              \
              : "0"((br_int_32)(__a)), "rm"((br_int_32)(__b)), "rm"((br_int_32)(__c)), "rm"((br_int_32)(__d)), "rm"((br_int_32)(__e)),          \
                "rm"((br_int_32)(__f)), "rm"((br_int_32)(__g)), "rm"((br_int_32)(__h))                                                \
              : "ebx", "ecx", "edx", "cc");                                                                            \
                                                                                                                       \
        __r;                                                                                                           \
    })
#endif
