#include <winsock2.h> // MUST BE INCLUDED BEFORE STDIO
#include <ws2tcpip.h>
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>

#define DEFAULT_BUFLEN 1024
#define DEFAULT_PORT 33333

// compile with -lws2_32 at the END of the command

SOCKET sock = INVALID_SOCKET; // global socket file descriptor

BOOL WINAPI int_handler(DWORD sig_type) {
  if (sig_type == CTRL_C_EVENT || sig_type == CTRL_BREAK_EVENT) {
    printf("\nClosing socket.");
    shutdown(sock, SD_SEND);
    closesocket(sock);
    WSACleanup();
  }
  return FALSE;
}

int __cdecl main(int argc, char** argv) {
  WSADATA wsa;
  // start version 2.2 of winsock
  int status = WSAStartup(MAKEWORD(2,2), &wsa); 
  if (status != 0) {
    printf("Failed to start winsock! Error code: %d\n", status);
    return 1;
  }
  SetConsoleCtrlHandler(int_handler, TRUE);

  return 0;
}  


