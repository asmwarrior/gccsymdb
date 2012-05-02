#define AFTERX(x) X_##x
#define XAFTERX(x) AFTERX(x)
#define TABLESIZE 1024
#define BUFSIZE TABLESIZE
#define COMMON(str) #str, str##_book;
#define EPRINTF(...) fprintf(stderr, __VA_ARGS__)
#define EPRINTF2(args...) fprintf(stderr, args)
