#!/usr/bin/python
# vim: foldmarker=<([{,}])> foldmethod=marker
import os
import sys
import subprocess

# It's conveinent to use the script by
#   CC=/prj.src/mygcc.py ./configure ... && make
# But since `configure' generally uses CC to compile some trivial files to test
# platform feature, as the result, import some junky definitions to database,
# so use it carefully.

PROJECT_ROOT = "/path/to/project/";
# See Makefile about the explanation of later variables.
# CROSSNG = "/home/zyf/src/crosstool-ng-1.19.0/mips";
GCC_BUILD_ROOT = "/home/zyf/gcc/host-i686-pc-linux-gnu/gcc/";
# To cross-ng (mips), GCC_BUILD_BIN = CROSSNG + "/install/bin/mips-zyf-linux-gnu-gcc";
GCC_BUILD_BIN = GCC_BUILD_ROOT + "/xgcc";

tmp = sys.argv[1:];
tmp.insert(0, "-fplugin-arg-symdb-dbfile=" + PROJECT_ROOT + "/gccsym.db");
tmp.insert(0, "-fplugin=" + PROJECT_ROOT + "/symdb.so");
# Caution: just like Makefile, only x86 need later line.
tmp.insert(0, "-B" + GCC_BUILD_ROOT);
tmp.insert(0, GCC_BUILD_BIN);
sys.exit(subprocess.call(tmp, stdout=sys.stdout, stderr=subprocess.STDOUT, shell=False));
