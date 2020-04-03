#define WIN32_MEAN_AND_LEAN

#include <winsock2.h> // MUST BE INCLUDED BEFORE STDIO
#include <ws2tcpip.h>
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>

#define ERRPRINT(MSG, CODE) printf(MSG " Error code: %d\n", CODE); exit(1);

#define DEFAULT_BUFLEN 16
#define DEFAULT_PORT 33333
#define ADDR_BUFLEN 32
#define BACKLOG 5
#define SRV "srv/"

// compile with -lws2_32 at the END of the command

SOCKET listen_sock = INVALID_SOCKET; // for receiving connections
SOCKET conn_sock = INVALID_SOCKET; // for maintaining a connection

BOOL WINAPI int_handler(DWORD sig_type) {
  if (sig_type == CTRL_C_EVENT || sig_type == CTRL_BREAK_EVENT) {
    printf("\nClosing sockets.\n");
    shutdown(listen_sock, SD_BOTH);
    shutdown(conn_sock, SD_BOTH);
    closesocket(listen_sock);
    closesocket(conn_sock);
    WSACleanup();
  }
  return FALSE;
}

int main(int argc, char** argv) {
  WSADATA wsa;
  // start version 2.2 of winsock
  int status = WSAStartup(MAKEWORD(2,2), &wsa); 
  if (status != 0) {
    ERRPRINT("Failed to start winsock!", status);
  }
  SetConsoleCtrlHandler(int_handler, TRUE); // install Ctrl-C handler

  // Create internet socket using reliable data connection, specifically using
  // TCP
  if ((listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    status = WSAGetLastError();
    ERRPRINT("Failed to create listening socket!", status);
  }

  // Set up server address object
  struct sockaddr_in serv_addr;
  int sin_size = sizeof(struct sockaddr_in);
  memset(&serv_addr, 0, sin_size);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(DEFAULT_PORT);
  // allow binding to any network interface
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  // Bind socket to address
  if (bind(listen_sock, (struct sockaddr *) &serv_addr, sin_size) < 0) {
    status = WSAGetLastError();
    ERRPRINT("Failed to bind listening socket!", status);
  }

  // Start listening
  if (listen(listen_sock, BACKLOG) < 0) {
    status = WSAGetLastError();
    ERRPRINT("Failed to start listening!", status);
  }

  // Handle connections
  while (1) {
    struct sockaddr_in client_addr;
    conn_sock = accept(listen_sock, (struct sockaddr *) &client_addr, 
        &sin_size);
    char client_addr_str[ADDR_BUFLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_addr_str, ADDR_BUFLEN);
    printf("Connection from %s\n", client_addr_str);

    // read and print data
    int conn_status = SOCKET_ERROR;
    char recvbuf[DEFAULT_BUFLEN + 1];
    size_t recvbuflen = DEFAULT_BUFLEN;
    do {
      conn_status = recv(conn_sock, recvbuf, (int) recvbuflen, 0);
      if (conn_status == SOCKET_ERROR) {
        ERRPRINT("Error receiving data!", WSAGetLastError());
      } else {
        printf("Received %d bytes.\n", conn_status);
        if (conn_status == 0) {
          printf("Connection closed by client.\n");
        } else {
          recvbuf[conn_status] = 0;
        }
      }

      printf("%s\n", recvbuf);

    } while (conn_status != 0);
  }

  return 0;
}  
