typedef void (*F)(void);
int volatile control = 1;
F f;
void foo(void) {}
void oof(void) {}
void ofo(void) {}
void foo_expr(void) {}
void oof_expr(void) {}
void ofo_expr(void) {}

union
{
	char c;
	F u_mem;
} u = {
	.u_mem = foo,
};

struct def
{
	F eme;
};

struct abc
{
	char c;
	F mem;
	struct def y;
	struct def z[2];
	F arr[2];
	F* pp;
} x = {
	// The second and the third are the same, gcc internal only stores a tree.
	.mem = foo,
	.y = { foo, },
	.y.eme = oof,

	// Later lines are too complex syntax.
	.z[0].eme = ofo,
	.arr[0] = ofo,
	.pp = (F*) ofo,
};

int main(void)
{
	// Simple member function pointer calling sample.
	(x.mem)();
	(x.arr[1])();
	(*(x.pp + 1))();

	x.mem = (F) ((int*) foo_expr); // The line is supported due to gcc internal simplifies syntax tree.
	x.y.eme = oof_expr;

	// Later lines are too complex syntax.
	foo_expr, x.mem = oof_expr;
	x.mem = oof_expr, ofo_expr;
	x.mem = control ? oof_expr : ofo_expr;
	x.pp = (F*) ofo_expr;
	x.arr[1] = ofo_expr;

	// assign function declaration not function pointer.
	struct abc* xp = &x;
	xp->mem = f;
	return 0;
}
