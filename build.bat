del CMakeCache.txt
del cmake_install.cmake
del Makefile
rmdir /s /q CMakeFiles
cmake . -DCMAKE_C_COMPILER="cl.exe"

