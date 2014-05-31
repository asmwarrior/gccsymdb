//*
#define M a

typedef int *funtd(void);
int *fundecl(void);
typedef int (*funtdp)(void);
int (*funpvar)(void);
typedef int itd;
typedef int (**funtdpcomplex[2][3])(void);
int arr[3][2];
int *p, * __attribute__((aligned))* const*foofoo(void);
extern int exi, exj;
;

int muldef;
int muldef;

struct abc;
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
struct abc;

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
	(*funpvar)();
	funpvar();
	return foo();
}

char c __attribute__((unused));
int ofo(int);
int ofo(int i __attribute__((unused))) {}
int ofo(int);
int* fof(int i __attribute__((unused))) {}
// Later case comes from sqlite-3.8/src/os.c:sqlite3OsDlSym,
// ararefuncdecl returns a function pointer whose prototype is short* (*)(char).
static short* (*ararefuncdecl(void *pVfs, float q))(char p);
static short* (*ararefuncdecl(void *pVfs, float q))(char p){
	return pVfs;
}

extern void off(void) { __LINE__; }
//*/
