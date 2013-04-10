.PHONY: default clean format db redo

# If only compile the plugin, you can ignore any error from `redo' target.

# Compiling the plugin need gmp.h, mpfr.h and so on.
# To cross-ng (mips), it should be crossng.build/mipsel-uxl-linux-gnu/build/static/.
MY_ROOT=/home/zyf/root/

# Plugin source directory.
SYMDB_ROOT=/home/zyf/src/symdb.gcc/

# To cross-ng(mips), it should be crossng.build/src/gcc-4.6.3/.
GCC_SRC=/home/zyf/gcc/
# To cross-ng (mips), it should be crossng.build/mipsel-uxl-linux-gnu/build/build-gcc/build-i686-build_pc-linux-gnu/libiberty/.
GCC_BUILD_LIB=/home/zyf/gcc/host-i686-pc-linux-gnu/libiberty/
# To cross-ng (mips), it should be crossng.build/mipsel-uxl-linux-gnu/build/build-gcc/gcc/.
GCC_BUILD_ROOT=/home/zyf/gcc/host-i686-pc-linux-gnu/gcc/

# To cross-toolchain, GCC_BUILD_BIN should be set to installation directory -- /opt/cross-mips/bin/mipsel-gcc.
# The variable is useful for run test.
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
	rm -f gccsym.db && ./gs initdb ./

format:
	# GNU code standard
	indent -nbad -bap -nbc -bbo -bl -bli2 -bls -ncdb -nce -cp1 -cs -di2 -ndj -nfc1 -nfca -hnl -i2 -ip5 -lp -pcs -psl -nsc -nsob so.c app.c

clean:
	rm -f *.ii *.i *.s *.o a.out gs *.h.gch *.so *.c~
	(cd test && ./run.sh clean)
	:> log.gdb
	rm -f *.db *.db-journal
