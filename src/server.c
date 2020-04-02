#include <winsock2.h> // MUST BE INCLUDED BEFORE STDIO
#include <ws2tcpip.h>
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#define DEFAULT_BUFLEN 1024
#define DEFAULT_PORT 33333

#define SRV "srv"

// compile with -lws2_32 at the END of the command

SOCKET sock = INVALID_SOCKET; // global socket file descriptor

void __cdecl int_handler(int dummy) {
  printf("\nClosing socket.");
  shutdown(sock, SD_SEND);
  closesocket(sock);
  WSACleanup();
  exit(0);
}

int __cdecl main(int argc, char** argv) {
  WSADATA wsa;
  // start version 2.2 of winsock
  int status = WSAStartup(MAKEWORD(2,2), &wsa); 
  if (status != 0) {
    printf("Failed to start winsock! Error code: %d\n", status);
    return 1;
  }
  signal(SIGINT, int_handler);
  
  return 0;
}  
