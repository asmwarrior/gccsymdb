#include <c.h>
#define X \
	AFTERX(BUFSIZE) \
	XAFTERX(BUFSIZE) \
	COMMON(uv) \
	EPRINTF("%d", 1) \
	EPRINTF2("%d", BUFSIZE)

void foo(void)
{
	X;
}
