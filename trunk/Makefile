.PHONY: default clean format db test quilt faccessvex

# If only compile the plugin, you can ignore any error from `db' target.

# CROSSNG=${HOME}/src/crosstool-ng-1.19.0/mips

# Compiling the plugin need gmp.h, mpfr.h and so on.
# To cross-ng (mips), MY_ROOT=${CROSSNG}/build/mips-zyf-linux-gnu/buildtools
MY_ROOT=${HOME}/root/

# Plugin source directory.
SYMDB_ROOT=${HOME}/src/symdb.gcc/

# To cross-ng (mips), GCC_SRC=${CROSSNG}/build/src/gcc-4.6.4
GCC_SRC=${HOME}/gcc/
# To cross-ng (mips), GCC_BUILD_LIB=${CROSSNG}/build/mips-zyf-linux-gnu/build/build-cc-final/libiberty/
GCC_BUILD_LIB=${HOME}/gcc/host-i686-pc-linux-gnu/libiberty/
# To cross-ng (mips), GCC_BUILD_ROOT=${CROSSNG}/build/mips-zyf-linux-gnu/build/build-cc-final/gcc/
GCC_BUILD_ROOT=${HOME}/gcc/host-i686-pc-linux-gnu/gcc/

# To cross-ng (mips), GCC_BUILD_BIN=${CROSSNG}/install/bin/mips-zyf-linux-gnu-gcc
# The variable is useful for run test.
GCC_BUILD_BIN=${GCC_BUILD_ROOT}/xgcc -B${GCC_BUILD_ROOT}

default: symdb.so symdbcxx.so gs

symdb.so: so.c
	gcc -Wall -O3 -ggdb $< ${GCC_BUILD_LIB}/libiberty.a -I. -I${GCC_SRC}/ -I${GCC_SRC}/gcc/ -I${GCC_SRC}/include -I${GCC_BUILD_ROOT}/ -I${GCC_SRC}/libcpp/ -I${GCC_SRC}/libcpp/include -I${MY_ROOT}/include -lsqlite3 -DIN_GCC -fPIC -shared -o $@
	# selinux specific!
	# chcon -t texrel_shlib_t symdb.so

gs: app.c
	gcc -Wall -O3 -ggdb -std=gnu99 $< -o $@ -D__FROM_CMDLINE_A="\"`cat ${GCC_SRC}/gcc/BASE-VER`\"" -D__FROM_CMDLINE_B="\"svn-`svn info | grep 'Last Changed Rev:' | awk '{print $$4}'`\"" -lsqlite3 -I${GCC_SRC}/ -I${GCC_SRC}/gcc/ -I${GCC_SRC}/include -I${GCC_BUILD_ROOT}/ ${GCC_BUILD_LIB}/libiberty.a

symdbcxx.so: so.c
	gcc -Wall -O3 -ggdb $< ${GCC_BUILD_LIB}/libiberty.a -I. -I${GCC_SRC}/ -I${GCC_SRC}/gcc/ -I${GCC_SRC}/include -I${GCC_BUILD_ROOT}/ -I${GCC_SRC}/libcpp/ -I${GCC_SRC}/libcpp/include -I${MY_ROOT}/include -lsqlite3 -DCXX_PLUGIN -DIN_GCC -fPIC -shared -o $@

db:
	./gs initdb ./
	${GCC_BUILD_BIN} -std=gnu99 --sysroot=${SYMDB_ROOT}/test/ a.c -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb || true
	# Sometime, -O3 can also produce errors.
	${GCC_BUILD_BIN} -std=gnu99 --sysroot=${SYMDB_ROOT}/test/ a.c -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -O3 || true
	./gs enddb ./

format:
	# GNU code standard
	indent -nbad -bap -nbc -bbo -bl -bli2 -bls -ncdb -nce -cp1 -cs -di2 -ndj -nfc1 -nfca -hnl -i2 -ip5 -lp -pcs -psl -nsc -nsob so.c app.c

clean:
	rm -f *.ii *.i *.s *.o a.out gs *.h.gch *.so *.c~
	(cd test && ./run.sh clean)
	:> log.gdb
	rm -f *.db *.db-journal

quilt:
	cd ${GCC_SRC} && QUILT_DIFF_OPTS=-pu quilt refresh --no-timestamps --sort

test:
	(cd test && ./run.sh)

faccessvex:
	./gs initdb ./
	./gs faccessv-expansion task_t mm
	${GCC_BUILD_BIN} -std=gnu99 --sysroot=${SYMDB_ROOT}/test/ a.c -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb || true
	./gs enddb ./
	./gs faccessv-expansion
