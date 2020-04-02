@REM To be run from an MSVC command prompt

cl src\server.c ws2_32.lib
cl src\client_p.c ws2_32.lib
cl src\client_np.c ws2_32.lib

move server.exe bin
move client_p.exe bin
move client_np.exe bin

move server.obj bin
move client_p.obj bin
move client_np.obj bin
