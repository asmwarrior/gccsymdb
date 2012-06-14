#!/bin/sh
if [ "$1" = "clean" ]; then
	find . -name 'new' -exec rm -f {} \;
	exit 0;
fi

GCC_ROOT=/home/zyf/gcc/
MY_ROOT=/home/zyf/root/
PATCH_ROOT=/home/zyf/src/symdb.gcc/
test_it ()
{
(cd ../ && ./gs initdb ./)
export LD_LIBRARY_PATH=${MY_ROOT}/lib/
(cd ../ && LD_LIBRARY_PATH=${MY_ROOT}/lib:${GCC_ROOT}/host-i686-pc-linux-gnu/libiberty/ ${GCC_ROOT}/host-i686-pc-linux-gnu/gcc/xgcc -B${GCC_ROOT}/host-i686-pc-linux-gnu/gcc/ --sysroot=${PATCH_ROOT}/test/ -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb test/$1/a.c)
(cd ../ && cat > abc123 << "EOF"
.output log.gdb
.du chFile
.du FileDependence
.du Definition
.du DefinitionRelationship
.du FileDefinition
.qu
EOF
cat abc123 | ${MY_ROOT}/bin/sqlite3 gccsym.db && rm -f abc123)
cp ../log.gdb $1/new
diff $1/orig $1/new || exit 1
}

find . -\( -name '*.h' -or -name '*.c' -\) -exec touch -t 201201010101.00 {} \;
test_it basic
echo PASS basic
test_it macro
echo PASS macro
test_it paren_declarator
echo PASS paren_declarator
test_it file_dependence
echo PASS file_dependence
test_it hash
echo PASS hash
test_it cpptoken
echo PASS cpptoken
