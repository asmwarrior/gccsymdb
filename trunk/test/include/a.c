int x, y, xx;
void foo(void)
{
#define y \
	2;
#include "a.h"
#undef y

#define y \
	x + xx;
#include "a.h"
#undef y

#define x \
	xx
#define y \
	9;
#include "a.h"
}
