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
#define SERV_IP_ADDR "127.0.0.1"

#define USR "usr"
#define PATH_LEN 255
#define FILE_CNT 3

// compile with -lws2_32 at the END of the command

SOCKET sock = INVALID_SOCKET; // for receiving connections

void close_cli() {
    printf("\nClosing sockets.\n");
    shutdown(sock, SD_BOTH);
    closesocket(sock);
    WSACleanup();
}

BOOL WINAPI int_handler(DWORD sig_type) {
  if (sig_type == CTRL_C_EVENT || sig_type == CTRL_BREAK_EVENT) {
    close_cli();
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
  if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    status = WSAGetLastError();
    ERRPRINT("Failed to create socket!", status);
  }

  // Specify the server to connect to
  char serv_ip_addr[ADDR_BUFLEN] = SERV_IP_ADDR;

  // Set up server address object
  struct sockaddr_in serv_addr;
  int sin_size = sizeof(struct sockaddr_in);
  memset(&serv_addr, 0, sin_size);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(DEFAULT_PORT);
  inet_pton(AF_INET, serv_ip_addr, &serv_addr.sin_addr);

  // Connect
  if (connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    status = WSAGetLastError();
    ERRPRINT("Failed to connect to server!", status);
  }

  // Send some text
  char buf[PATH_LEN] = USR;
  char *bufptr = buf + strlen(USR);
  const char *filenames[FILE_CNT] = {"/a.jpg", "/b.mp3", "/c.txt"};
  const char *mime[FILE_CNT] = {"img/jpeg", "audio/mp3", "text/plain"};

  for (int i = 0; i < FILE_CNT; ++i) {
    strncpy(bufptr, filenames[i], strlen(filenames[i]));
    if (send(sock, buf, strlen(buf), 0) < 0) {
      status = WSAGetLastError();
      ERRPRINT("Failed to send data!", status);
    }
  }

  close_cli();
  return 0;
}  
