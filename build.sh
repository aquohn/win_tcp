#!/bin/sh

rm cmake_install.cmake
rm CMakeCache.txt
rm -rf CMakeFiles
cmake -G "MSYS Makefiles" .
