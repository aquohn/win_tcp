del CMakeCache.txt
del cmake_install.cmake
del Makefile
rmdir /s /q CMakeFiles
cmake -G "MinGW Makefiles" -DCMAKE_SH="CMAKE_SH-NOTFOUND" .
