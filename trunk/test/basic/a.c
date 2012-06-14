//*
#define M a

typedef int td;
typedef int (**tdfunp[2][3])(void);
int arr[3][2];
int (**funparrvar[2][3])(void);
int (*funpvar)(void);
int *p, * __attribute__((aligned))* const*foofoo(void);
extern int exi, exj;
;

int muldef;
int muldef;

struct abc {
	char c;
	int i;
} v1 = {.c = 2}, v2[3];
enum {
	enumx = 2,
	enumy
} __attribute__((packed));
struct def {};
typeof(struct ghi {char c; int i;}) tp;

void oldfun(abc)
int ((abc));
{
}
int foo(void)
{
	return enumx;
}

int main(void)
{
	int (*innerfunpvar[2][3])(void);
	typedef int (*innertdfun)(void);
	__builtin_trap();
	(*funparrvar[0][1])();
	funpvar();
	return foo();
}
//*/
