#include <stdio.h>
#include <stdlib.h>

#include "defs.h"

ep_noreturn void _ep_abort(const char* cond, const char* file, unsigned int line, const char* func)
{
    fprintf(stderr, "%s:%u: %s: Assertion '%s' failed\n", file, line, func, cond);
    abort();
}
