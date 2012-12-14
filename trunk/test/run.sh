#!/bin/sh
# vim: foldmarker=<([{,}])> foldmethod=marker
if [ "$1" = "clean" ]; then
	find . -name 'new' -exec rm -f {} \;
	exit 0;
fi

# dump_helper function <([{
dump_helper ()
{
	if [ "$1" = "file" ]; then
		sed 's/uvwxyz/.du chFile\n.du FileDependence/' abc123 > 123abc
	elif [ "$1" = "def" ]; then
		sed 's/uvwxyz/.du Definition\n.du DefinitionRelationship\n.du FileDefinition/' abc123 > 123abc
	elif [ "$1" = "ifdef" ]; then
		sed 's/uvwxyz/.du chFile\n.du Ifdef/' abc123 > 123abc
	elif [ "$1" = "falias" ]; then
		sed 's/uvwxyz/.du FunpAlias\n.du Definition/' abc123 > 123abc
	fi
	mv 123abc abc123
}
# }])>

# test_it function <([{
MY_ROOT=/home/zyf/root/
PATCH_ROOT=/home/zyf/src/symdb.gcc/
GCC_BUILD_ROOT=/home/zyf/gcc/host-i686-pc-linux-gnu/gcc/
GCC_BUILD_BIN="${GCC_BUILD_ROOT}/xgcc -B${GCC_BUILD_ROOT}/"
test_it ()
{
(cd ../ && ./gs initdb ./)
(cd ../ && ${GCC_BUILD_BIN} --sysroot=${PATCH_ROOT}/test/ -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb test/$1/a.c)
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
test_it ifdef ifdef
echo PASS ifdef
test_it basic def
echo PASS basic
test_it macro def
echo PASS macro
test_it paren_declarator def
echo PASS paren_declarator
test_it file_dependence file
echo PASS file_dependence
test_it hash def
echo PASS hash
test_it cpptoken def
echo PASS cpptoken
test_it funp_alias falias
echo PASS funp_alias
