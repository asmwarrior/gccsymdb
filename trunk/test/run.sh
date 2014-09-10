#!/bin/sh
# vim: foldmarker=<([{,}])> foldmethod=marker
SYMDB_ROOT=/home/zyf/src/symdb.gcc/
GCC_BUILD_ROOT=/home/zyf/gcc/host-i686-pc-linux-gnu/gcc/
GCC_BUILD_BIN="${GCC_BUILD_ROOT}/xgcc -B${GCC_BUILD_ROOT}/"
GXX_BUILD_BIN="${GCC_BUILD_ROOT}/g++ -B${GCC_BUILD_ROOT}/"

if [ "$1" = "clean" ]; then
	find . -name 'new' -exec rm -f {} \;
	exit 0;
fi

# dump_helper function <([{
dump_helper ()
{
	if [ "$1" = "file" ]; then
		sed 's/uvwxyz/.du chFile\n.du FileDependence/' abc123 > 123abc
	elif [ "$1" = "fcallf" ]; then
		sed 's/uvwxyz/.du Definition\n.du FunctionCall/' abc123 > 123abc
	elif [ "$1" = "faccessv" ]; then
		sed 's/uvwxyz/.du FunctionAccess\n.du FunctionPattern/' abc123 > 123abc
	elif [ "$1" = "def" ]; then
		sed 's/uvwxyz/.du Definition\n/' abc123 > 123abc
	elif [ "$1" = "ifdef" ]; then
		sed 's/uvwxyz/.du chFile\n.du Ifdef/' abc123 > 123abc
	elif [ "$1" = "falias" ]; then
		sed 's/uvwxyz/.du FunctionAlias\n.du Definition\n.du FunctionCall/' abc123 > 123abc
	elif [ "$1" = "offsetof" ]; then
		sed 's/uvwxyz/.du Definition\n.du Offsetof/' abc123 > 123abc
	fi
	mv 123abc abc123
}
# }])>

# test_it function <([{
test_it ()
{
(cd ../ && ./gs initdb ./)
if [ "$1" = "faccessvex" ]; then
	(cd ../ && ./gs faccessv-expansion task_t mm)
fi
if [ "$3" = "gcc" ]; then
	(cd ../ && ${GCC_BUILD_BIN} --sysroot=${SYMDB_ROOT}/test/ -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb test/$1/a.c)
	(cd ../ && ${GCC_BUILD_BIN} --sysroot=${SYMDB_ROOT}/test/ -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -O3 test/$1/a.c)
else
	(cd ../ && ${GXX_BUILD_BIN} --sysroot=${SYMDB_ROOT}/test/ -fplugin=./symdbcxx.so -fplugin-arg-symdbcxx-dbfile=./gccsym.db -ggdb test/$1/a.c)
	(cd ../ && ${GXX_BUILD_BIN} --sysroot=${SYMDB_ROOT}/test/ -fplugin=./symdbcxx.so -fplugin-arg-symdbcxx-dbfile=./gccsym.db -O3 test/$1/a.c)
fi
(cd ../ && ./gs enddb ./)
(cd ../ && cat > abc123 << "EOF"
.output log.gdb
uvwxyz
.qu
EOF
dump_helper $2
cat abc123 | sqlite3 gccsym.db && rm -f abc123)
cp ../log.gdb $1/new
diff $1/orig $1/new || exit 1
}
# }])>

find . -\( -name '*.h' -or -name '*.c' -\) -exec touch -t 201201010101.00 {} \;
test_it faccessvex faccessv gcc
echo PASS faccessvex
test_it faccessv faccessv gcc
echo PASS faccessv
test_it fcallf fcallf gcc
echo PASS fcallf
test_it ifdef ifdef gcc
echo PASS ifdef
test_it def def gcc
echo PASS def
test_it macro def gcc
echo PASS macro
test_it parendef def gcc
echo PASS parendef
test_it filedep file gcc
echo PASS filedep
test_it hash def gcc
echo PASS hash
test_it cpptoken def gcc
echo PASS cpptoken
test_it falias falias gcc
echo PASS falias
test_it offsetof offsetof gcc
echo PASS offsetof
test_it cxx offsetof g++
echo PASS cxx
