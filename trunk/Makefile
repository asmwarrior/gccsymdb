.PHONY: default clean format db

GCC_ROOT=/home/zyf/src/gcc-4.6.2/
MY_ROOT=/home/zyf/root/
PATCH_ROOT=/home/zyf/src/symdb.gcc/
default:
	gcc -save-temps -ggdb symdb.c ${GCC_ROOT}/host-i686-pc-linux-gnu/libiberty/libiberty.a -I. -I${GCC_ROOT}/ -I${GCC_ROOT}/gcc/ -I${GCC_ROOT}/include -I${GCC_ROOT}/host-i686-pc-linux-gnu/gcc/ -I${GCC_ROOT}/libcpp/ -I${GCC_ROOT}/libcpp/include -I${MY_ROOT}/include -L${MY_ROOT}/lib -lsqlite3 -DIN_GCC -fPIC -shared -o symdb.so
	# selinux specific!
	# chcon -t texrel_shlib_t symdb.so
	make db && LD_LIBRARY_PATH=${MY_ROOT}/lib:${GCC_ROOT}/host-i686-pc-linux-gnu/libiberty/ ${GCC_ROOT}/host-i686-pc-linux-gnu/gcc/xgcc -B${GCC_ROOT}/host-i686-pc-linux-gnu/gcc/ --sysroot=${PATCH_ROOT}/test/ a.c -fplugin=./symdb.so -fplugin-arg-symdb-dbfile=./gccsym.db -ggdb

db:
	rm -f gccsym.db && ${MY_ROOT}/bin/sqlite3 -init init.sql gccsym.db ""

format:
	# GNU code standard
	indent -nbad -bap -nbc -bbo -bl -bli2 -bls -ncdb -nce -cp1 -cs -di2 -ndj -nfc1 -nfca -hnl -i2 -ip5 -lp -pcs -psl -nsc -nsob symdb.c

clean:
	rm -f *.ii *.i *.s *.o a.out *.h.gch *.so symdb.c~
	(cd test && ./run.sh clean)
	:> log.gdb
	rm -f *.db *.db-journal

sync:
	(cd ${GCC_ROOT} && quilt refresh)
	cp -u ${GCC_ROOT}/patches/* gcc.patches
