#include <winsock2.h> // MUST BE INCLUDED BEFORE STDIO
#include <ws2tcpip.h>
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <signal.h>

int sockfd; // global socket file descriptor

int main(int argc, char** argv) {
  WSADATA wsa;
  // start version 2.2 of winsock
  int status = WSAStartup(MAKEWORD(2,2), &wsa); 
  if (status != 0) {
    printf("Failed to start winsock! Error code: %d\n", status);
    return 1;
  }

  return 0;
}  
