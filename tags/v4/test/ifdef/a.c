#define Z
#include "a.h"
#undef Z
#include "a.h"

#if 0
#define X x
int X;
#else
#define Y y
int Y;
#endif

int z;
