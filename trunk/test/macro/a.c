//*
#define MUL_DEFINITION \
	int i; \
	int j; \
	int main(
MUL_DEFINITION void) {
}

int base;
int cancel;
#define base(p) foo()
#define null(p)

int foo(void)
{
#define cancel(p) foo()
	cancel = 1;
#define cancel_inmacro(p) base + 1
	cancel_inmacro(1);

#define cascaded_head base
	cascaded_head(2);
#define cascaded_head2 base(2)
	cascaded_head2;
#define cascaded_middle 3 + base + 3
	cascaded_middle;
#define cascaded_tail null(4) 3 + base
	cascaded_tail(2);
#define cascaded_tail2 null(4) 3 + base(2)
	cascaded_tail2;
#define cascaded_func_head(p) base
	cascaded_func_head(2)(2);
#define cascaded_func_tail(p) base + 3 + base
	cascaded_func_tail(2)(2);

	// cascaded + cancel.
	cascaded_head;
	cascaded_tail;
	cascaded_func_head(2);
	cascaded_func_tail(2);

	// 3-level cascaded.
#define c3_head cascaded_head
	c3_head(2);
#define c3_tail null(4) cascaded_tail
	c3_tail(2);

#define paste(x, y) x ## y
	paste(ba, se)(1);

	// macro expansion in directive clause.
#define cond(p) 1
#define cascaded_cond cond
#if cascaded_cond(1) == 1
#endif

	// macro expansion in macro args.
#define args(x, y) 1
	args(base, cascaded_head(1));

	// Case just for torture purpose.
#define torture(p) p + base(
	torture(1)2);
	return 0;
}
//*/
