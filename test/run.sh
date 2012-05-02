#!/bin/sh
if [ "$1" = "clean" ]; then
	find . -name 'new' -exec rm -f {} \;
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

(cd ../ && rm -f gccsym.db && ${MY_ROOT}/bin/sqlite3 -init init.sql gccsym.db "")
export LD_LIBRARY_PATH=${MY_ROOT}/lib/
(cd ../ && LD_LIBRARY_PATH=${MY_ROOT}/lib:${GCC_ROOT}/host-i686-pc-linux-gnu/libiberty/ ${GCC_ROOT}/host-i686-pc-linux-gnu/gcc/xgcc -B${GCC_ROOT}/host-i686-pc-linux-gnu/gcc/ --sysroot=${PATCH_ROOT}/test/ -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb test/helper/a.c)
(rm helper/new)
(cd ../ && ./gs def a.c foo >> test/helper/new)
(cd ../ && ./gs def -- foo >> test/helper/new)
(echo "caller" >> helper/new)
(cd ../ && ./gs caller -- main >> test/helper/new)
(cd ../ && ./gs caller -- foo >> test/helper/new)
(echo "callee" >> helper/new)
(cd ../ && ./gs callee -- main >> test/helper/new)
(cd ../ && ./gs callee -- foo >> test/helper/new)
diff helper/orig helper/new || exit 1
echo PASS helper
