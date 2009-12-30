struct X
{
	int* const m;
	char n;
	char o[];
};

struct X x;
static const int ** const * volatile y[5][3];
int i = 0;
typedef int my;
static unsigned char (*foo)(short ** (*)(long long), int);

int main(void)
{
	int c = 0, j = 1;
    if (c == 3) {
        int k;
        k = x.n;
    } else {
        *x.m += c + i * j;
    }
	return 0;
}
