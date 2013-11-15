int i, j, k;
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
	int* p;
} abc;
int arr[1][2][3];
const int ci = 8;

void bad_get1(int p1, int p2)
{
	i = p1 + p2;
}
int bad_get2(void)
{
	// Here, only address and pointer operators can be prefixed.
	return i++;
}
void bad_get3(int p1)
{
	int m;
	i = p1;
}
void bad_set1(int p1)
{
	*abc.p = p1 + 1;
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
	int m, n;
	struct {
		char c;
	}  def = { i };
	typeof(i, def)* ptypeof;
	i = *p;
	i = sizeof(i, j);
	!m;
	i = i + (int) &i;
	i = j / 10, j % 10;
	i = ci;
	;
	f1 = f2 / f3;
	i = j && k;
	i = j | k;
	*(abc.p + 3) = 3;
	*((int*) (abc.x + m)) = 3;
	*((int*) (abc.x + m) + n) = 3;
	i = +m;
	i = -n;
	i = !j ? m : k;
	*p = 3;
	i = j = 3;
	i = (unsigned int) &j + k++ + n;
	i = abc.arr[1][1].z.arr2[1];
	i = arr[0][1][2];
	set_p(m);
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
} x, y, *px;
int** pp;
struct X* ofo(void)
{
	return 0;
}
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
	((struct X*) (x.pp[j]))->cp;
	((struct X*) i)->p[j].c;
	(x = lx).p;
	(lx, x).p;
	(j, (struct X*) i)->p;
	((x = lx).p, x.p)->c;
	(&x.v)->c;
	ofo()->v;
	p[i/8] |= 3;
	(px->next ? px->next : px)->p;
}

#include<stdarg.h>
va_list ap;
void oof(char* file, ...)
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
