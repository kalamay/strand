#include "ctx.h"

#include <inttypes.h>

#if __SIZEOF_POINTER__ == 8
# define FMTxREG "0x%016" PRIxPTR
#elif __SIZEOF_POINTER__ == 4
# define FMTxREG "0x%08" PRIxPTR
#endif

#if defined(__x86_64__)
# include "ctx/x86_64.c"
#endif
