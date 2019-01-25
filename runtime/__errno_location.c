#include <errno.h>

static int errno_val;

int *__errno_location(void)
{
	return &errno_val;
}

weak_alias(__errno_location, ___errno_location);
