#!/bin/sh
if [ "$1" = "clean" ]; then
	find . -name 'new' -exec rm -f {} \;
	exit 0;
elif [ "$1" = "diff" ]; then
	echo "Case include"
	diff include/orig include/new
	echo "Case macro"
	diff macro/orig macro/new
	echo "Case sysmacro"
	diff sysmacro/orig sysmacro/new
	echo "Case advmacro"
	diff advmacro/orig advmacro/new
	echo "Case cascaded"
	diff cascaded/orig cascaded/new
	exit 0;
fi

GCC_ROOT=/home/zyf/src/gcc-4.6.2/
MY_ROOT=/home/zyf/root/
PATCH_ROOT=/home/zyf/src/symdb.gcc/
test_it ()
{
(cd ../ && rm -f gccsym.db && ${MY_ROOT}/bin/sqlite3 -init init.sql gccsym.db "")
export LD_LIBRARY_PATH=${MY_ROOT}/lib/
(cd ../ && LD_LIBRARY_PATH=${MY_ROOT}/lib:${GCC_ROOT}/host-i686-pc-linux-gnu/libiberty/ ${GCC_ROOT}/host-i686-pc-linux-gnu/gcc/xgcc -B${GCC_ROOT}/host-i686-pc-linux-gnu/gcc/ --sysroot=${PATCH_ROOT}/test/ -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb test/$1/a.c)
(cd ../ && cat > abc123 << "EOF"
.output log.gdb
.du
.qu
EOF
cat abc123 | ${MY_ROOT}/bin/sqlite3 gccsym.db && rm -f abc123)
cp ../log.gdb $1/new
}

test_it macro
test_it advmacro
test_it sysmacro
test_it include
test_it cascaded
