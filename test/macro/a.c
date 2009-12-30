#define u
#define z \
	(i##i)
#define y(a) \
	(a * a)

void x(int i) { }

// backslash test, some spaces following the first and third backslashs.
#define x(a, b) \   
	(b + y(a) \
	 + \ 
	 b)

// some spaces following this and next lines.  
int main(void)   
{
	int c, i, j, ii;
	c = x(i, j);

	/* note: macro has a high priority than variable and function in cpp stage. 
	 * there're some spaces left the line. */  
	int x = u z;
	x u = x(i, j);

	return __LINE__;
}
