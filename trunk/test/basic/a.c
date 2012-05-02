//*
#define M a

typedef int td;
typedef int (**tdfunp[2][3])(void);
int arr[3][2];
int (**funpvar[2][3])(void);
int *p, * __attribute__((aligned))* const*foofoo(void);
extern int exi, exj;
;

int muldef;
struct abc {
	char c;
	int i;
} v1 = {.c = 2}, v2[3];
enum {
	enumx = 2,
	enumy
} __attribute__((packed));

int muldef;
int foo(void)
{
	return enumx;
}

int main(void)
{
	int (*innerfunpvar[2][3])(void);
	typedef int (*innertdfun)(void);
	__builtin_trap();
	(*funpvar[0][1])();
	return foo();
}
//*/
