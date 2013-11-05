#!/usr/bin/python
# vim: foldmarker=<([{,}])> foldmethod=marker
import os
import sys
import subprocess

PROJECT_ROOT = "/path/to/project/";
GCC_BUILD_ROOT = "/home/zyf/gcc/host-i686-pc-linux-gnu/gcc/";
GCC_BUILD_BIN = GCC_BUILD_ROOT + "/xgcc";

tmp = sys.argv[1:];
tmp.insert(0, "-fplugin-arg-symdb-dbfile=" + PROJECT_ROOT + "/gccsym.db");
tmp.insert(0, "-fplugin=" + PROJECT_ROOT + "/symdb.so");
# Just like Makefile, only x86 need later line.
tmp.insert(0, "-B" + GCC_BUILD_ROOT);
tmp.insert(0, GCC_BUILD_BIN);
sys.exit(subprocess.call(tmp, stdout=sys.stdout, stderr=subprocess.STDOUT, shell=False));
