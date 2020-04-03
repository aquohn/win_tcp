#!/bin/sh

rm cmake_install.cmake
rm CMakeCache.txt
rm -rf CMakeFiles
rm *.vcxproj
rm *.filters
rm *.sln
cmake -G "MSYS Makefiles" .
