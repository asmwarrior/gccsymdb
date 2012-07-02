.PHONY: default clean format db

GCC_SRC=/home/zyf/gcc/
MY_ROOT=/home/zyf/root/
SYMDB_ROOT=/home/zyf/src/symdb.gcc/
# Later is used by cross-toolchain.
GCC_BUILD_LIB=/home/zyf/gcc/host-i686-pc-linux-gnu/libiberty/
GCC_BUILD_INC=/home/zyf/gcc/host-i686-pc-linux-gnu/gcc/
# To cross-toolchain, GCC_BUILD_BIN should be set to installation directory.
GCC_BUILD_BIN=/home/zyf/gcc/host-i686-pc-linux-gnu/gcc/

default:
	gcc -Wall -ggdb so.c ${GCC_BUILD_LIB}/libiberty.a -I. -I${GCC_SRC}/ -I${GCC_SRC}/gcc/ -I${GCC_SRC}/include -I${GCC_BUILD_INC}/ -I${GCC_SRC}/libcpp/ -I${GCC_SRC}/libcpp/include -I${MY_ROOT}/include -L${MY_ROOT}/lib -lsqlite3 -DIN_GCC -fPIC -shared -o symdb.so
	gcc -Wall -ggdb -std=c99 app.c -o gs -L${MY_ROOT}/lib -lsqlite3 -I${MY_ROOT}/include -I${GCC_SRC}/ ${GCC_BUILD_LIB}/libiberty.a
	# selinux specific!
	# chcon -t texrel_shlib_t symdb.so
	./gs initdb ./ && LD_LIBRARY_PATH=${MY_ROOT}/lib ${GCC_BUILD_BIN}/xgcc -B${GCC_BUILD_BIN}/ --sysroot=${SYMDB_ROOT}/test/ a.c -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb

db:
	rm -f gccsym.db && ${MY_ROOT}/bin/sqlite3 -init init.sql gccsym.db ""

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
