typedef char v8qi __attribute__ ((__vector_size__ (8)));
int i, j, k;
unsigned int ui;
int *p, *q;
float f1, f2, f3;
struct
{
	int x;
	struct {
		int y;
		struct {
			int arr2[5];
		} z;
	} arr[3][4];
	v8qi v[2];
	int* p;
} abc;
int arr[1][2][3];
const int ci = 8;

// Function patter testcases.
void bad_get1(int p1, int p2)
{
	i = p1 + p2;
}
int bad_get2(void)
{
	// Here, only address and pointer operators can be prefixed.
	return ++i;
}
void bad_get3(int p1)
{
	int m; // Not local variable.
	i = p1;
}
void bad_set1(int p1)
{
	*abc.p = p1 + 1; // assigment expression is too complex.
}

// Three kinds of getX, return address/pointer/read.
int* get_abc(void)
{
	return &abc.arr[2][1].y;
}
int get_p(void)
{
	return *p;
}
int get_arr(void)
{
	return arr[0][0][0];
}
void set_abc(int p1)
{
	abc.arr[2][1].y = p1;
}
void set_p(int p1)
{
	*p = p1;
}

int main(void)
{
	__label__ __here; __here: &&__here;
	int m, n;
	struct {
		char c;
	}  def = { i };
	i = *p;
	!m;
	i = i + (int) &i;
	j / 10, j % 10;
	i = ci;
	;
	f1 = f2 / f3;
	j && k;
	k | j;
	*(abc.p + 3) = 3;
	*((int*) (abc.x + m)) = 3;
	*((int*) (abc.x + m) + n) = 3;
	i = +m;
	i = -n;
	i = !j ? m : k;
	*p = 3;
	i = j = 3;
	(unsigned int) &j + k++ + n;
	abc.arr[1][1].z.arr2[1];
	arr[0][1][2];
	abc.arr[i][j].y;
	set_p(m);
	ui = (ui << 7) | (ui >> (32 - 7));
	__builtin_choose_expr(__builtin_types_compatible_p (typeof (i), double), j, k);
	__builtin_offsetof(typeof(abc), v[k]);
	__builtin_offsetof(typeof(abc), arr[i][j].z.arr2[k]);
	return 0;
}

struct Y {
	char c;
};
struct X {
	struct X* next;
	char *cp;
	struct Y* p;
	struct Y** pp;
	struct Y v;
	void* jump_ptr;
	struct { int anon_i; };
} x, y, *px;
int** pp;
void* jump_ptr;
struct X* ofo(void)
{
	return 0;
}
struct X off(void)
{
	return x;
}
union ucast
{
	int* i;
};
void foo(char* parm1, struct X* parm2)
{
	struct X lx;
	x.p->c = 0;
	parm1 += 2;
	*parm1 = *parm2->cp;
	int lc = (x.p[i].c, x.p[y.p[j].c].c);
	int **lp = &*pp;
	--*pp; **pp++;
	**pp = 0;
	struct Y ly = *x.p;
	p[i] = 3;
	foo(&y.p[j].c, (struct X*) 0);
	i = p - q;
	f1 = i;
	i = f1;
	(i > 0 ? i : 0) < 3;
	(x = lx).p;
	(lx, x).p;
	(j, (struct X*) i)->p;
	((x = lx).p, x.p)->c;
	(&x.v)->c;
	off().v;
	ofo()->v;
	p[i/8] |= 3;
	x.anon_i;
	px->anon_i;
	(px->next ? px->next : px)->p;

	((struct X*) (x.pp[j]))->cp;
	((struct X*) i)->p[j].c;
	((union ucast) p).i;
}

#include<stdarg.h>
va_list ap;
void va(char* file, ...)
{
	va_list aq;
	va_start(ap, file);
	i = va_arg(ap, int);
	va_copy(ap, aq);
	va_end(ap);
}

struct
{
	unsigned bf1 : 2;
	unsigned bf2 : 3;
	unsigned bf3 : 2;
} bfv;
void bfr_test(void)
{
	i = bfv.bf1 == 1;
	i = bfv.bf1 == 2 && bfv.bf3 == 3;
}

float stmt_in_expr(void)
{
	struct X lx; int li, lj;
	(*({ &x.v; })).c;
	(*({ li < lj; &x.v; })).c;
	({ li < lj; y; lx = y, y; }).v;
	({ li < lj; ({ li > lj; y.p; }); })->c;
	({ li < lj; y; }).v;
	({ li < lj; px; })->v;
	({ li < lj; x = y = lx; }).v;
	({ li = k; (li < i) ? x : y; }).v;
	({ ofo(); })->v;
	({ struct X* _t = px; &px[3]; })->v.c;
	({ x; }).v; // case without TARGET_EXPR at all.

	// Not in a leaf node.
	({ if (i) i++; });

	({struct X* const _t = px; _t; })->v.c |= 1;

	// COMPOUND_LITERAL_EXPR, later are an initializer statment, not compound statment.
	((struct inner_type { int ii; int ij; }) { .ii = i + li }).ij;
	(struct inner_type2 { int ii; }) { li };

	({ }); // BIND_EXPR + STATMENT_LIST (null)
	({
	struct Y lx = { .c = i, }, ly = { .c = i, }; // two DECL_EXPR with VAR_DECL
	int li = j, iter;

	switch (ui) { // SWITCH_EXPR (arg0, arg1 = statement_list)
		case 0: // <stmt> CASE_LABEL_EXPR, arg0 is const expression, arg1 is a LABEL_EXPR.
			i = 1;
			break;
		default: // CASE_LABEL_EXPR, arg0 is null.
			i = 2;
	}

	// Not like switch clause which has a SWITCH_EXPR tree, if, for, do, while is just consist of laber, cond, goto and statement block.
	if (i > 1) // COND_EXPR + STATMENT_LIST. To if ... else if, COND_EXPR + COND_EXPR.
		j = li;
	else if (i == 0)
		j = 1;
	else
		j = 0;
	for (; i;) {
		j = 0;
	}
	for (iter = ci; iter < i; iter++) {
		j = 1;
	}
	do {
		i = 1;
	} while (i);
	while (i) {
		j = li + i;
		if (j > 3) 
			continue;
	}
	return f1; // RETURN_EXPR + MODIFY_EXPR (internal variable as target).

	asm volatile ("movl %1, %0" : "=m" (j) : "r" (f2++, i)); // ASM_EXPR
	asm goto ("jmp %l0" : : : : failed); // asm goto clause must use label identifier.
	jump_ptr = &&failed; // ADDR_EXPR + LABEL_EXPR
failed:
	goto *x.anon_i; // CONVERT_EXPR typeof POINTER_TYPE
	goto *x.jump_ptr; // COMPONENT_REF typeof POINTER_TYPE / LABEL_EXPR
	goto *jump_ptr; // VAR_DECL.
	goto *(i = j, x.jump_ptr); // COMPOUND_EXPR.
	x;
	}).v;
	return 0;
}

union tu
{
	int tui;
} __attribute__((transparent_union));
void fun_tu(union tu p) {}

void fun_nested(void)
{
	void fun_in_fun(void)
	{
		fun_tu(i);
	}
}

void truth_notif_expr(char c)
{
	int m, n;
	i = (__builtin_constant_p (c) && ((c) == '\0'));
	i = ((i && j <= k) == (m == n));
}

void __attribute__ ((__target__ ("sse"))) sse(void)
{
	v8qi a, b, c;
	a = __builtin_ia32_pcmpeqb(b, c);
}

unsigned int target;
char source[4];
void mem_ref(void)
{
	__builtin_memcpy(&target, source, 4);
}

// List which trees can be the operand of INDIRECT_REF.
void indirect_ref(void)
{
	q = *++pp; // INDIRECT_REF + PREINCREMENT_EXPR
	q = *pp++; // INDIRECT_REF + POSTINCREMENT_EXPR, first *p, then p++.

	(&*px); // NON_LVALUE_EXPR + VAR_DECL
	(&*px)->v; // INDIRECT_REF + VAR_DECL
	*&px; // VAR_DECL
	({ &x; })->v; // INDIRECT_REF + C_MAYBE_CONST_EXPR + ADDR_EXPR + VAR_DECL, it's gnu extension of c99 6.5.2, and TARGET_EXPR + BIND_EXPR can substitute C_MAYBE_CONST_EXPR.
	*(p + 32); // INDIRECT_REF + POINTER_PLUS_EXPR
}
