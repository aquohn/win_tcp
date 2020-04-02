del CMakeCache.txt
del cmake_install.cmake
rmdir /s /q CMakeFiles
@REM cmake -G "NMake Makefiles" .
cmake -G "Ninja" . -DCMAKE_C_COMPILER="cl.exe"
@REM cmake .

