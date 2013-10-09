typedef void (*F)(void);
void foo(void) {}
struct
{
	F mem;
} x;

int main(void)
{
	foo();
	(x.mem)();
	return 0;
}
