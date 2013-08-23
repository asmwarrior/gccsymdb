struct a
{
	char c;
	int b : 3;
	int i;
	int b2 : 7;
	short s;
	char arr[3];
	int z;
} __attribute__((packed));

struct b
{
	char c;
	int b : 3;
	int i __attribute__((packed));
	int b2 : 7 __attribute__((packed));
	short s __attribute__((aligned(16)));
	char arr[3];
	int z;
};

struct c
{
};

struct d
{
	struct a d1;
	long long d2;
	struct {
		struct b d3anon1;
		struct c d3anon2;
	} d3;
	struct dnest {
		struct b d4nest1;
		struct c d4nest2;
	} d4;
	union {
		struct b du1;
		long du2;
		struct {
			int du3nest;
		} du3;
	};
	int : 32;
	struct {
		union {
		};
	} z;
};

union u
{
	struct a u1;
	long long u2;
};

struct
{
	int i;
	int j;
} anonv;

struct X {
	int i;
};
typedef struct X x_t;

typedef struct {
	int j;
} y_t;

typedef struct Z {
    int k;
} z_t;
