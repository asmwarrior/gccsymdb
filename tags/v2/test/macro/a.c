//*
#define MUL_DEFINITION \
	int i; \
	int j; \
	int foo(
MUL_DEFINITION void) {
}

#define a(p) x
#define b y
#define c
#define d(p) z
#define e(p) w

// cascaded.
#define ma a
int ma(int);
#define mb b
int mb;
#define mc a(int)
int mc;
#define md d(int)
int md(int);

// cascaded + cancel.
#define na e = 1
int na;
int ma = 1;

// three-layer cascaded.
#define oa ma(int)
int oa;
#define ob mb
int ob;
#define oc md(int)
int oc;

// some disturbers.
#define pa c ma(int)
int pa;
#define pb c mb
int pb;
#define pc c md(int)
int pc;
//*/
