.PHONY: default clean format db test

# If only compile the plugin, you can ignore any error from `db' target.

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

default: symdb.so symdbcxx.so gs

symdb.so: main.c
	gcc -Wall -O3 -ggdb $< ${GCC_BUILD_LIB}/libiberty.a -I. -I${GCC_SRC}/ -I${GCC_SRC}/gcc/ -I${GCC_SRC}/include -I${GCC_BUILD_ROOT}/ -I${GCC_SRC}/libcpp/ -I${GCC_SRC}/libcpp/include -I${MY_ROOT}/include -lsqlite3 -DIN_GCC -fPIC -shared -o $@
	# selinux specific!
	# chcon -t texrel_shlib_t symdb.so

gs: app.c
	a=`cat ${GCC_SRC}/gcc/BASE-VER`; b=`svn info | grep 'Last Changed Rev:' | awk '{print $$4}'`; sed "s:<@a@>:$$a:" $< | sed "s:<@b@>:$$b:" > _$<;
	gcc -Wall -O3 -ggdb -std=gnu99 _$< -o $@ -lsqlite3 -I${GCC_SRC}/ ${GCC_BUILD_LIB}/libiberty.a

symdbcxx.so: main.c
	gcc -Wall -O3 -ggdb $< ${GCC_BUILD_LIB}/libiberty.a -I. -I${GCC_SRC}/ -I${GCC_SRC}/gcc/ -I${GCC_SRC}/include -I${GCC_BUILD_ROOT}/ -I${GCC_SRC}/libcpp/ -I${GCC_SRC}/libcpp/include -I${MY_ROOT}/include -lsqlite3 -DCXX_PLUGIN -DIN_GCC -fPIC -shared -o $@

db:
	./gs initdb ./
	${GCC_BUILD_BIN} -std=gnu99 --sysroot=${SYMDB_ROOT}/test/ a.c -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb || true
	# Sometime, -O3 can also produce errors.
	${GCC_BUILD_BIN} -std=gnu99 --sysroot=${SYMDB_ROOT}/test/ a.c -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -O3 || true
	./gs enddb ./

format:
	# GNU code standard
	indent -nbad -bap -nbc -bbo -bl -bli2 -bls -ncdb -nce -cp1 -cs -di2 -ndj -nfc1 -nfca -hnl -i2 -ip5 -lp -pcs -psl -nsc -nsob main.c app.c

clean:
	rm -f *.ii *.i *.s *.o a.out gs *.h.gch *.so *.c~ _app.c
	(cd test && ./run.sh clean)
	:> log.gdb
	rm -f *.db *.db-journal

quilt:
	cd ${GCC_SRC} && QUILT_DIFF_OPTS=-pu quilt refresh --no-timestamps --sort

test:
	(cd test && ./run.sh)
