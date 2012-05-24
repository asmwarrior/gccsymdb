#define a(x) #x
#define b a(.ascii "123")
static void foo(void)
{
	asm volatile (".ascii \"123\"\n"
			b "\n");
}
