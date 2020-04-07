#define WIN32_MEAN_AND_LEAN

#include <winsock2.h> // MUST BE INCLUDED BEFORE STDIO
#include <ws2tcpip.h>
#include <stdio.h> 
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>

#define WT_DIE(MSG, CODE) printf("%s Error code: %d\n", MSG, CODE); exit(1)
#define WT_QUIT(MSG, CODE) close_serv(); WT_DIE(MSG, CODE)

#define DEBUG 1

#define debug_print(...) do { if (DEBUG) fprintf(stderr, __VA_ARGS__); } while (0)

#define DEFAULT_PORT 33333
#define DATA_BUFLEN 1024
#define DATA_BUFLEN_STR "1024"
#define ADDR_BUFLEN 32
#define FIELD_BUFLEN 128
#define FIELD_BUFLEN_STR "128"
#define HEXSTR_MAXLEN 64
#define BACKLOG 5

// some Windows macro I'm not using
#ifdef DELETE
#undef DELETE
#endif
#define SRV "srv"
#define SRV_LEN 3
#define FILE_CNT 3

#define HTTP_BAD_REQ 400
#define HTTP_NOT_FOUND 404
#define HTTP_WRONG_MTD 404
#define HTTP_NOT_ACC 406
#define HTTP_TOO_LARGE 431
#define HTTP_WRONG_VER 505
#define HTTP_OK 200
#define HTTP_NOT_MOD 304

enum http_mtd {GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH,
  MTD_COUNT}; // for keeping track of number of methods
char *http_mtd_strs[] = {"GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT",
  "OPTIONS", "TRACE", "PATCH"};

void close_serv();
BOOL WINAPI int_handler(DWORD sig_type);
FILE *locate(char *url, char *accept, int *wrong_type);
bool parse_timestamp(char *timestamp, struct tm *timestruct);
FILE *handle_req(const char *req, enum http_mtd *mtd, char **data, 
    int *err);
char *_strcat(char *destination, const char *source);
char *_strcpy(char *destination, const char *source);

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
    int recv_status = SOCKET_ERROR;
    char recvbuf[DATA_BUFLEN + 1];
    while (1) {
      recv_status = recv(conn_sock, recvbuf, DATA_BUFLEN, 0);
      if (recv_status == SOCKET_ERROR) {
        WT_QUIT("Error receiving data!", WSAGetLastError());
      } else if (recv_status == 0) {
        printf("Connection closed by client.\n");
        break;
      } 

      printf("Read %d bytes.\n", recv_status);
      recvbuf[recv_status] = 0;

      debug_print("Data received: %s\n", recvbuf);

      int err_code;
      char *req_data;
      FILE *serv_file = NULL;
      // establish request type
      serv_file = handle_req(recvbuf, &req_mtd, &req_data, 
          &err_code);
      // something went wrong when prcoessing the header
      if (serv_file == NULL) {
        // TODO send error code to HTTP client
        WT_QUIT("Error parsing request!", err_code);
      }
      
      // only implementing GET for this assignment
      // send in chunks
      // format is <chunk size in hex>\r\n<chunk>\r\n
      if (req_mtd == GET) {

        struct stat file_status;
        fstat(fileno(serv_file), &file_status);
        size_t full_chunks = file_status.st_size / DATA_BUFLEN;
        size_t last_chunk_size = file_status.st_size % DATA_BUFLEN;

        // TODO write response line
        char sendbuf[DATA_BUFLEN + HEXSTR_MAXLEN + 4 + 1];
        char *sendbufcurr, *sendbufend;
        
        _strcpy(sendbuf, "HTTP/1.1 200 OK\r\n");

        char buflen_hexstr[HEXSTR_MAXLEN + 2 + 1];
        sprintf(buflen_hexstr, "%X\r\n", DATA_BUFLEN);
        size_t full_hexstr_len = strlen(buflen_hexstr);

        int status;

        debug_print("Writing out chunks...\n");

        // write full chunks
        if (full_chunks > 0) {
          sendbufcurr = _strcpy(sendbuf, buflen_hexstr);
          size_t c;
          for (c = 0; c < full_chunks; ++c) {
            sendbufend = sendbufcurr + fread(sendbuf + full_hexstr_len, 
                DATA_BUFLEN, 1, serv_file);
            strcpy(sendbufend, "\r\n");
            if (send(conn_sock, sendbuf, full_hexstr_len + DATA_BUFLEN + 2, 0) < 0) {
              WT_QUIT("Failed to send data!", WSAGetLastError());
            }
            debug_print("Sent full chunk: %s\n", sendbuf);
          }
        }

        // write last chunk
        sprintf(buflen_hexstr, "%X\r\n", last_chunk_size);
        size_t last_hexstr_len = strlen(buflen_hexstr);
        sendbufcurr = _strcpy(sendbuf, buflen_hexstr);
        sendbufend = sendbufcurr + fread(sendbufcurr, last_chunk_size, 
            1, serv_file);
        strcpy(sendbufend, "\r\n");
        if (send(conn_sock, sendbuf, last_hexstr_len + last_chunk_size + 2, 0) < 0) {
          WT_QUIT("Failed to send data!", WSAGetLastError());
        }
        debug_print("Sent last chunk: %s\n", sendbuf);

        const char *chunk_end = "0\r\n\r\n";
        // signal end of chunks
        if (send(conn_sock, chunk_end, strlen(chunk_end), 0) < 0) {
          WT_QUIT("Failed to send data!", WSAGetLastError());
        }
        debug_print("Sent end of chunk: %s\n", chunk_end);

        break;
      }
    } 
  }
  return 0;
}  

/**
 * Parse a HTTP 1.1 request, returning a file handler to the relevant file that
 * needs to be served, indicating the HTTP method used, the position where the
 * data starts, and an error message string.
 */
FILE *handle_req(const char *req, enum http_mtd *mtd, char **data, 
    int *err) {

  char urlbuf[FIELD_BUFLEN + 1], mtdbuf[FIELD_BUFLEN + 1], 
  verbuf[FIELD_BUFLEN + 1]; 
  int linelen; 
  FILE *resource = NULL;
  time_t cli_mod_time;
  int if_mod_since = 0;

  // check validity and mark start of data
  char *end = strstr(req, "\r\n\r\n");
  if (end == NULL) {
    debug_print("No header terminator!");
    *err = HTTP_BAD_REQ;
    return NULL;
  }
  *data = end + strlen("\r\n\r\n");

  // parse first line
  if (sscanf(req, "%" FIELD_BUFLEN_STR "s %" FIELD_BUFLEN_STR "s %" 
        FIELD_BUFLEN_STR "s \r\n%n", mtdbuf, urlbuf, verbuf, &linelen) != 3) {
    debug_print("Wrong request line!");
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
    *err = HTTP_WRONG_MTD;
    return NULL;
  }

  // check HTTP version
  // only supporting HTTP/1.1
  if (strcmp(verbuf, "HTTP/1.1") != 0) {
    *err = HTTP_WRONG_VER;
    return NULL;
  }

  // read header lines
  // only supporting Accept and If-Modified-Since
  char hdrbuf[FIELD_BUFLEN], valbuf[DATA_BUFLEN];

  while (req < end) {
    if (sscanf(req, " %" FIELD_BUFLEN_STR "[^ :\r\n]: %" DATA_BUFLEN_STR 
          "[^\r\n] \r\n%n", hdrbuf, valbuf, &linelen) != 2) {
      debug_print("Invalid header line: %s\n", req);
      *err = HTTP_BAD_REQ;
      return NULL;
    }
    req += linelen;
    debug_print("Processing %s...\n", hdrbuf);

    // lowercase field names for comparison
    char *p;
    for (p = hdrbuf; *p != 0; ++p) {
      *p = tolower(*p);
    }

    // check various supported headers
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
      struct tm mod;
      if (!parse_timestamp(valbuf, &mod)) {
        debug_print("Invalid timestamp: %s\n", valbuf);
        *err = HTTP_BAD_REQ;
        return NULL;
      }
      cli_mod_time = mktime(&mod);
      if_mod_since = 1;
    }
  }

  if (resource == NULL) {
    resource = locate(urlbuf, NULL, NULL);
  }

  // still NULL implies error
  if (resource == NULL) {
    *err = HTTP_NOT_FOUND;
    return NULL;
  }

  // check last modified
  if (if_mod_since) {
    struct stat resource_status; 
    fstat(fileno(resource), &resource_status);
    if (resource_status.st_mtime <= cli_mod_time) {
      fclose(resource);
      *err = HTTP_NOT_MOD;
      return NULL;
    }
  }

  debug_print("All checks passed, returning resource.\n");
  return resource;    
}

/**
 * Locates a resource, returning its file pointer.
 */
FILE *locate(char *url, char *accept, int *wrong_type) {
  debug_print("Finding file %s...\n", url);
  const char *filenames[FILE_CNT] = {"/a.jpg", "/b.mp3", "/c.txt"};
  const char *mime[FILE_CNT] = {"img/jpeg", "audio/mp3", "text/plain"};
  char path[FIELD_BUFLEN + SRV_LEN + 1] = SRV;
  if (wrong_type) {
    *wrong_type = 0;
  }

  int i;
  for (i = 0; i < FILE_CNT; ++i) {
    if (strcmp(url, filenames[i]) == 0) {
      if (accept && (strcmp(accept, mime[i]) != 0) && wrong_type) {
        *wrong_type = 1;
        return NULL;
      } else {
        strcpy(path + SRV_LEN, filenames[i]);
        debug_print("Full path: %s\n", path);
        return fopen(path, "rb");
      }
    }
  }
  return NULL;
}

/**
 * Parses a timestamp in the recommended format.
 */
bool parse_timestamp(char *timestamp, struct tm *timestruct) {
  // yday and wday are ignored
  timestruct->tm_isdst = 0;
  char month[4];
  int year;

  // can be optimised with a proper hash table
  char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
    "Sep", "Oct", "Nov", "Dec"};

  if (sscanf(timestamp, "%*3s, %d %3s %d %d:%d:%d GMT", &timestruct->tm_mday,
        month, &year, &timestruct->tm_hour, &timestruct->tm_min, 
        &timestruct->tm_sec) != 6) {
    return false;
  }
  timestruct->tm_year = year - 1900;
  int i;
  for (i = 0; i < 12; ++i) {
    if (strcmp(month, months[i]) == 0) {
      timestruct->tm_mon = i;
      break;
    }
  }
  if (i == 12) {
    return false;
  }

  return true;
}

/**
 * Interrupt handler for Ctrl-C or window close events, implemented
 * Windows-style because it needs to interact with its API.
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

/**
 * strcpy that returns pointer to null byte of concatenated strings.
 */
char *_strcpy(char *destination, const char *source) {
  char s = *source;
  while (s != 0) {
    *(destination++) = s;
    s = *(++source);
  }
  *(++destination) = 0;
  return destination;
}

/**
 * strcat that returns pointer to null byte of concatenated strings.
 */
char *_strcat(char *destination, const char *source) {
  while (*destination != 0) {
    ++destination;
  }
  return _strcpy(destination, source);
}
