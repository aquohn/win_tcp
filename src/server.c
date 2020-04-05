#define WIN32_MEAN_AND_LEAN

#include <winsock2.h> // MUST BE INCLUDED BEFORE STDIO
#include <ws2tcpip.h>
#include <stdio.h> 
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>

#define WT_DIE(MSG, CODE) printf("%s Error code: %d\n", MSG, CODE); exit(1)
#define WT_QUIT(MSG, CODE) close_serv(); WT_DIE(MSG, CODE)

#define DATA_BUFLEN 1024
#define DEFAULT_PORT 33333
#define ADDR_BUFLEN 32
#define FIELD_BUFLEN 128
#define FIELD_BUFLEN_STR "128"
#define BACKLOG 5

// some Windows macro I'm not using
#ifdef DELETE
#undef DELETE
#endif
#define SRV "srv"
#define FILE_CNT 3

#define HTTP_BAD_REQ 400
#define HTTP_NOT_FOUND 404
#define HTTP_NOT_ACC 406
#define HTTP_TOO_LARGE 431
#define HTTP_OK 200
#define HTTP_NOT_MOD 304

enum http_mtd {GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH,
  MTD_COUNT}; // for keeping track of number of methods
char *http_mtd_strs[] = {"GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT",
  "OPTIONS", "TRACE", "PATCH"};

void close_serv();
BOOL WINAPI int_handler(DWORD sig_type);
FILE *locate(char *url, char *accept, int *wrong_type);
FILE * handle_req(const char *req, enum http_mtd *mtd, char **data, 
    int *err);

// compile with -lws2_32 at the END of the command

SOCKET listen_sock = INVALID_SOCKET; // for receiving connections
SOCKET conn_sock = INVALID_SOCKET; // for maintaining a connection

int main(int argc, char** argv) {
  WSADATA wsa;
  // start version 2.2 of winsock
  int status = WSAStartup(MAKEWORD(2,2), &wsa); 
  if (status != 0) {
    WT_DIE("Failed to start winsock!", status);
  }
  SetConsoleCtrlHandler(int_handler, TRUE); // install Ctrl-C handler

  // Create internet socket using reliable data connection, specifically using
  // TCP
  if ((listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    status = WSAGetLastError();
    WT_DIE("Failed to create listening socket!", status);
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
    WT_DIE("Failed to bind listening socket!", status);
  }

  // Start listening
  if (listen(listen_sock, BACKLOG) < 0) {
    status = WSAGetLastError();
    WT_DIE("Failed to start listening!", status);
  } 

  printf("Server listening on port %u!\n\n", DEFAULT_PORT);

  // Handle connections
  while (1) {
    struct sockaddr_in cli_addr;
    conn_sock = accept(listen_sock, (struct sockaddr *) &cli_addr, 
        &sin_size);
    char cli_addr_str[ADDR_BUFLEN];
    inet_ntop(AF_INET, &cli_addr.sin_addr, cli_addr_str, ADDR_BUFLEN);
    printf("Connection from %s\n", cli_addr_str);
    enum http_mtd req_mtd;

    // Read in data
    int conn_status = SOCKET_ERROR;
    char recvbuf[DATA_BUFLEN + 1];
    size_t recvbuflen = DATA_BUFLEN;
    do {
      conn_status = recv(conn_sock, recvbuf, (int) recvbuflen, 0);
      if (conn_status == SOCKET_ERROR) {
        WT_QUIT("Error receiving data!", WSAGetLastError());
      } else if (conn_status == 0) {
        printf("Connection closed by client.\n");
      } else {
        printf("Read %d bytes.\n", conn_status);
        recvbuf[conn_status] = 0;

        int err_code;
        char *req_data;
        FILE *serv_file = NULL;
        // establish request type
        if (serv_file == NULL) {
          serv_file = handle_req(recvbuf, &req_mtd, &req_data, 
              &err_code);
          // something went wrong when prcoessing the header
          if (serv_file == NULL) {
            // TODO send bad request to HTTP client
            WT_QUIT("Error parsing request!", err_code);
          }
        }

        // only implementing GET for this assignment


      }
    } while (conn_status != 0);
  }
  return 0;
}  

/**
 * Parse a HTTP 1.1 request, returning a file handler to the relevant file that
 * needs to be served, indicating the HTTP method used, the position where the
 * data starts, and an error message string.
 */
FILE * handle_req(const char *req, enum http_mtd *mtd, char **data, 
    int *err) {

  char urlbuf[FIELD_BUFLEN + 1], mtdbuf[FIELD_BUFLEN + 1], 
  verbuf[FIELD_BUFLEN + 1]; 
  int linelen; 
  FILE *resource = NULL;

  // check validity and mark start of data
  char *end = strstr(req, "\r\n\r\n");
  if (end == NULL) {
    *err = HTTP_BAD_REQ;
    return NULL;
  }
  *data = end + sizeof("\r\n\r\n");

  // parse first line
  if (sscanf(req, "%" FIELD_BUFLEN_STR "s %" FIELD_BUFLEN_STR "s %" 
        FIELD_BUFLEN_STR "s \r\n%n", mtdbuf, urlbuf, verbuf, &linelen) != 3) {
    *err = HTTP_BAD_REQ;
    return NULL;
  }
  req += linelen;

  // identify type of request
  // can be made marginally more efficient by keying the methods on length
  int i;
  for (i = 0; i < (int) MTD_COUNT; ++i) {
    if (strcmp(mtdbuf, http_mtd_strs[i]) == 0) {
      *mtd = (enum http_mtd) i;
      break;
    }
  }
  if (i == (int) MTD_COUNT) {
    *err = HTTP_BAD_REQ;
    return NULL;
  }

  // read header lines
  // only supporting Accept and If-Modified-Since
  char acceptbuf[DATA_BUFLEN], modbuf[DATA_BUFLEN];
  char hdrbuf[FIELD_BUFLEN], valbuf[DATA_BUFLEN];

  while (req < end) {
    if (sscanf(req, "%" FIELD_BUFLEN_STR "s: %" FIELD_BUFLEN_STR "[^\r\n] \r\n%n",
          hdrbuf, valbuf, &linelen) != 2) {
      *err = HTTP_BAD_REQ;
      return NULL;
    }
    req += linelen;

    // lowercase
    char *p;
    for (p = hdrbuf; *p != 0; ++p) {
      *p = tolower(*p);
    }

    if (strcmp(hdrbuf, "accept") == 0) {
      int wrong_type;
      resource = locate(urlbuf, valbuf, &wrong_type);
      if (resource == NULL) {
        if (wrong_type) {
          *err = HTTP_NOT_ACC;
        } else {
          *err = HTTP_NOT_FOUND;
        }
        return NULL;
      }
    } else if (strcmp(hdrbuf, "if-modified-since") == 0) {
      
    }
  }

  // only implementing GET for specified filetypes for assignment
  // supporting conditional GET and multithreading

  if (*mtd == GET) {

  }

  *err = HTTP_BAD_REQ; // method not supported
  return NULL;
}

/**
 * Locates a resource, returning its file pointer.
 */
FILE *locate(char *url, char *accept, int *wrong_type) {

}

/**
 * Interrupt handler for Ctrl-C or window close events.
 */
BOOL WINAPI int_handler(DWORD sig_type) {
  if (sig_type == CTRL_C_EVENT || sig_type == CTRL_BREAK_EVENT) {
    close_serv();
  }
  return FALSE;
}

/**
 * Close down server connections and clean up.
 */
void close_serv() {
  printf("\nClosing sockets.\n");
  shutdown(listen_sock, SD_BOTH);
  shutdown(conn_sock, SD_BOTH);
  closesocket(listen_sock);
  closesocket(conn_sock);
  WSACleanup();
}
