.PHONY: default clean format db

MY_ROOT=/home/zyf/root/
SYMDB_ROOT=/home/zyf/src/symdb.gcc/
GCC_SRC=/home/zyf/gcc/
GCC_BUILD_LIB=/home/zyf/gcc/host-i686-pc-linux-gnu/libiberty/
GCC_BUILD_ROOT=/home/zyf/gcc/host-i686-pc-linux-gnu/gcc/
# To cross-toolchain, GCC_BUILD_BIN should be set to installation directory.
# GCC_BUILD_BIN=/opt/cross-mips/bin/mipsel-gcc
GCC_BUILD_BIN=${GCC_BUILD_ROOT}/xgcc -B${GCC_BUILD_ROOT}

default:
	gcc -Wall -ggdb so.c ${GCC_BUILD_LIB}/libiberty.a -I. -I${GCC_SRC}/ -I${GCC_SRC}/gcc/ -I${GCC_SRC}/include -I${GCC_BUILD_ROOT}/ -I${GCC_SRC}/libcpp/ -I${GCC_SRC}/libcpp/include -I${MY_ROOT}/include -lsqlite3 -DIN_GCC -fPIC -shared -o symdb.so
	a=`cat ${GCC_SRC}/gcc/BASE-VER`; b=`svn info | grep 'Last Changed Rev:' | awk '{print $$4}'`; sed "s:<@a@>:$$a:" app.c | sed "s:<@b@>:$$b:" > _app.c;
	gcc -Wall -ggdb -std=c99 _app.c -o gs -lsqlite3 -I${GCC_SRC}/ ${GCC_BUILD_LIB}/libiberty.a
	rm _app.c
	# selinux specific!
	# chcon -t texrel_shlib_t symdb.so
	make db
	make redo

redo:
	${GCC_BUILD_BIN} --sysroot=${SYMDB_ROOT}/test/ a.c -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb

db:
	rm -f gccsym.db && sqlite3 -init init.sql gccsym.db ""

format:
	# GNU code standard
	indent -nbad -bap -nbc -bbo -bl -bli2 -bls -ncdb -nce -cp1 -cs -di2 -ndj -nfc1 -nfca -hnl -i2 -ip5 -lp -pcs -psl -nsc -nsob so.c app.c

clean:
	rm -f *.ii *.i *.s *.o a.out gs *.h.gch *.so *.c~
	(cd test && ./run.sh clean)
	:> log.gdb
	rm -f *.db *.db-journal

sync:
	(cd ${GCC_SRC} && quilt refresh)
	cp -u ${GCC_SRC}/patches/* gcc.patches
