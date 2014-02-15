typedef void (*F)(void);
typedef void (FF)(void);
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
	FF *ff;
	struct def y;
	struct def* py;
	struct def z[2];
	F arr[2];
	F* pp;
} x = {
	// Initialize list sample.
	.mem = foo,
	.ff = foo,
	// The later two lines are the same, gcc internal only stores a tree -- `.y.eme = oof'.
	.y = { foo, },
	.y.eme = oof,
	// Later lines are too complex syntax.
	.z[0].eme = ofo,
	.arr[0] = ofo,
	.pp = (F*) ofo,
};
struct abc* jump;

struct ghi
{
	F mem;
	struct def y;
} y = { foo, { foo } };

int main(void)
{
	jump = &x;

	// calling sample.
	(x.mem)();
	(*jump->mem)();
	(jump->mem)();
	(jump->y.eme)();
	(jump->py->eme)();
	(x.y.eme)();
	(x.py->eme)();
	// Later lines are too complex syntax.
	(*x.pp)();
	(x.arr[1])();
	(*(x.pp + 1))();

	// Assign expression sample.
	x.mem = (F) ((int*) foo_expr); // The line is supported due to gcc internal simplifies syntax tree.
	x.ff = foo_expr;
	x.y.eme = oof_expr;
	// Later lines are too complex syntax.
	foo_expr, x.mem = oof_expr;
	x.mem = oof_expr, ofo_expr;
	x.mem = control ? oof_expr : ofo_expr;
	x.pp = (F*) ofo_expr;
	x.arr[1] = ofo_expr;
	// And don't assign function pointer, just function declaration.
	x.mem = f;
	return 0;
}
