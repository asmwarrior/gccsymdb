typedef struct {
	int arr;
} map_t;

typedef struct {
	map_t* map;
} mm_t;

typedef struct {
	mm_t* mm;
	struct {
		int i;
	};
} task_t;

task_t init;
task_t* foo(map_t* p)
{
	return (task_t*) p;
}
typedef task_t* (*FUNC)(map_t*);
FUNC pfunc = foo;

int main(void)
{
	pfunc((map_t*) 0)->mm = (void*) 0; // CALL_EXPR + VAR_DECL
	foo((map_t*) 0)->mm = (void*) 0; // CALL_EXPR + ADDR_EXPR + FUNC_DECL
	task_t* p; p->i = 0; p->mm->map->arr = 1;
	mm_t* p2; p2->map = (void*) 0;
	task_t* p3; p3->mm = (void*) 0;
	p3->i = 3;
	init.mm = (void*) 0;
	return 0;
}
