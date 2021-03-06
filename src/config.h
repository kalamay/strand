#ifndef STRAND_CONFIG_H
#define STRAND_CONFIG_H

#if defined (__unix__)
# include <sys/param.h>
#endif

#if defined (__BLOCKS__)
# define STRAND_BLOCKS 1
# include <Block.h>
#endif

#if !defined (NDEBUG) && defined (STRAND_USE_VALGRIND)
# define STRAND_VALGRIND 1
# include <valgrind/valgrind.h>
#endif

#if defined (__APPLE__) && defined (__MACH__)
# define STRAND_MACOSX 1
#elif defined (__linux__)
# define STRAND_LINUX 1
#elif defined (__FreeBSD__)
# define STRAND_BSD 1
# define STRAND_FREEBSD 1
#elif defined (__DragonFly__)
# define STRAND_BSD 1
# define STRAND_DFLYBSD 1
#else
# error platform not supported
#endif

#if defined (__ARM_ARCH_7__) || \
    defined (__ARM_ARCH_7R__) || \
    defined (__ARM_ARCH_7A__)
# define STRAND_ARM7 1
#endif

#if defined (STRAND_ARMV7) || \
    defined (__ARM_ARCH_6__) || \
    defined (__ARM_ARCH_6J__) || \
    defined (__ARM_ARCH_6K__) || \
    defined (__ARM_ARCH_6Z__) || \
    defined (__ARM_ARCH_6T2__) || \
    defined (__ARM_ARCH_6ZK__)
# define STRAND_ARM6 1
#endif

#if defined (STRAND_ARMV6) || \
    defined (__ARM_ARCH_5T__) || \
    defined (__ARM_ARCH_5E__) || \
    defined (__ARM_ARCH_5TE__) || \
    defined (__ARM_ARCH_5TEJ__)
# define STRAND_ARM5 1
#endif

#if defined (STRAND_ARMV5) || \
    defined (__ARM_ARCH_4__) || \
    defined (__ARM_ARCH_4T__)
# define STRAND_ARM4 1
#endif

#if defined (STRAND_ARMV4) || \
    defined (__ARM_ARCH_3__) || \
    defined (__ARM_ARCH_3M__)
# define STRAND_ARM3 1
#endif

#if defined (STRAND_ARMV3) || \
    defined (__ARM_ARCH_2__)
# define STRAND_ARM2 1
#endif

#if STRAND_ARM2
# define STRAND_ARM 1
#elif defined (__amd64__) || defined (__x86_64__) || defined (_M_X64) || defined (_M_AMD64)
# define STRAND_X86_64 1
#elif defined (__i386__) || defined (_M_IX86) || defined (_X86_)
# define STRAND_X86_32 1
#endif

#endif

