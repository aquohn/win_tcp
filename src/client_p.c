// compile with -lws2_32 at the END of the command

#define WIN32_MEAN_AND_LEAN

#ifdef _WIN32
#define LL_FMT "I64"
#else
#define LL_FMT "ll"
#endif

#include <winsock2.h> // MUST BE INCLUDED BEFORE STDIO
#include <ws2tcpip.h>
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

#define WT_INFO(MSG, CODE) fprintf(stderr, "%s Error code: %d\n", MSG, CODE)
#define WT_DIE(MSG, CODE) WT_INFO(MSG, CODE); close_cli(); exit(1)
#define WT_BADCHUNK() fprintf(stderr, "Invalid chunking format!\n"); goto closedown

#define DEBUG 0

#define debug_print(...) do { if (DEBUG) fprintf(stderr, __VA_ARGS__); } while (0)
#define append(str1, str2) str1 = _strcpy(str1, str2)

#define SERV_IP_ADDR "127.0.0.1"
#define DEFAULT_PORT 33333
#define DATA_BUFLEN 512
#define DATA_BUFLEN_STR "512"
#define ADDR_BUFLEN 32
#define FIELD_BUFLEN 128
#define FIELD_BUFLEN_STR "128"
#define HEXSTR_MAXLEN 64
#define BACKLOG 5

// some Windows macro I'm not using
#ifdef DELETE
#undef DELETE
#endif
#define USR "usr"
#define FILE_CNT 3

#define HTTP_BAD_REQ 400
#define HTTP_NOT_FOUND 404
#define HTTP_WRONG_MTD 405
#define HTTP_NOT_ACC 406
#define HTTP_TOO_LARGE 431
#define HTTP_WRONG_VER 505
#define HTTP_OK 200
#define HTTP_NOT_MOD 304
#define HTTP_TIME_LEN 29

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

enum http_mtd {GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH,
  MTD_COUNT}; // for keeping track of number of methods
const char *http_mtd_strs[] = {"GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT",
  "OPTIONS", "TRACE", "PATCH"};
enum chunk_state {STATE_SIZE, STATE_SIZE_R, STATE_DATA, STATE_DATA_R}; 
const char *filenames[FILE_CNT] = {"/a.jpg", "/b.mp3", "/c.txt"};
const char *mime[FILE_CNT] = {"img/jpeg", "audio/mp3", "text/plain"};

/**
 * Struct representing data parsed from request header. Only contains supported
 * information fields. Supported headers are Accept, Connection,
 * If-Modified-Since
 */
struct reqinfo {
  enum http_mtd mtd;
  char url[FIELD_BUFLEN + 1];
  char accept[FIELD_BUFLEN + 1];
  bool keep_alive;
  bool if_mod_since;
  time_t if_mod_since_time;
  char *data;
};

FILE *setup_get_resource(struct reqinfo *info);
size_t write_get_req(char *req, struct reqinfo *info);
bool parse_resp(char *resp, char *res_type, bool *is_chunked);
void close_cli();
BOOL WINAPI int_handler(DWORD sig_type);
char *_strcat(char *destination, const char *source);
char *_strcpy(char *destination, const char *source);

SOCKET sock = INVALID_SOCKET; // for receiving connections

int main(int argc, char** argv) {
  WSADATA wsa;
  // start version 2.2 of winsock
  int status = WSAStartup(MAKEWORD(2,2), &wsa); 
  if (status != 0) {
    WT_DIE("Failed to start winsock!", status);
  }
  SetConsoleCtrlHandler(int_handler, TRUE); // install Ctrl-C handler

  time_t mod_since = 1;

  if (argc == 2) {
    if (strcmp(argv[1], "now") == 0) {
      mod_since = time(0);
    } else {
      mod_since = strtoull(argv[1], NULL, 10);
    }
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

  // Create internet socket using reliable data connection, specifically using
  // TCP
  if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    WT_DIE("Failed to create socket!", WSAGetLastError());
  }

  // Connect
  if (connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    WT_DIE("Failed to connect to server!", WSAGetLastError());
  }

  // setup common request features
  struct reqinfo info;
  char reqbuf[DATA_BUFLEN + 1];
  size_t reqlen;
  FILE *recv_file = NULL;

  info.mtd = GET;
  info.data = NULL;
  info.keep_alive = true;
  info.if_mod_since = true;
  info.if_mod_since_time = mod_since; // beginning of time

  for (int i = 0; i < FILE_CNT; ++i) {
    printf("\n");
    if (DEBUG && i != 2) {
      continue;
    }

    // generate file request
    strcpy(info.url, filenames[i]);
    strcpy(info.accept, mime[i]);
    recv_file = setup_get_resource(&info);
    if (!recv_file) {
      fprintf(stderr, "Failed to open %s for writing!\n", info.url);
      continue;
    }
    reqlen = write_get_req(reqbuf, &info);

    printf("Sending request:\n%s\n", reqbuf);

    // request file
    if (send(sock, reqbuf, reqlen, 0) < 0) {
      status = WSAGetLastError();
      WT_INFO("Failed to send request!", WSAGetLastError());
      continue;
    }

    // handle response
    int recv_status = SOCKET_ERROR;
    char recvbuf[DATA_BUFLEN + 1];
    bool is_chunked = false;
    bool is_hdr_read = false;
    char *recvcurr = NULL, *recvend = NULL;
    char chunkbuf[FIELD_BUFLEN + 1];
    char *chunkbufcurr = chunkbuf;
    size_t chunklen = 0;
    enum chunk_state state = STATE_SIZE;

    while (1) {
      recv_status = recv(sock, recvbuf, DATA_BUFLEN, 0);
      if (recv_status == SOCKET_ERROR) {
        WT_INFO("Error receiving data!", WSAGetLastError());
        break;
      } else if (recv_status == 0) {
        printf("Connection closed by server.\n");
        break;
      } 

      recvcurr = recvbuf;
      recvend = recvbuf + recv_status;
      *recvend = '\0';

      if (!is_hdr_read) {
        is_hdr_read = parse_resp(recvbuf, info.accept, &is_chunked);
        if (!is_hdr_read) { // error while reading
          break;
        }
      } else {
        if (is_chunked) {
          while (recvcurr < recvend) {
            char curr = *(recvcurr++);
            if (chunklen == 0) {
              switch (state) {
                case STATE_SIZE: 
                  if (curr == '\r') {
                    *(chunkbufcurr++) = curr;
                    state = STATE_SIZE_R;
                  } else if (!isxdigit(curr)) {
                    WT_BADCHUNK();
                  } else {
                    *(chunkbufcurr++) = curr;
                  }
                  break;
                case STATE_SIZE_R:
                  if (curr != '\n') {
                  } else if (*chunkbuf == '0' && *(chunkbuf + 1) == '\r') {
                    printf("All chunks read, closing file.\n");
                    goto closedown;
                  } else {
                    chunklen = strtoull(chunkbuf, NULL, 16);     
                    if (chunklen == 0) {
                      WT_BADCHUNK();
                    }
                  }
                  chunkbufcurr = chunkbuf;
                  state = STATE_DATA;
                  break;
                case STATE_DATA: 
                  if (curr == '\r') {
                    state = STATE_DATA_R;
                  } else {
                    WT_BADCHUNK();
                  }
                  break;
                case STATE_DATA_R: 
                  if (curr == '\n') {
                    state = STATE_SIZE;
                  } else {
                    WT_BADCHUNK();
                  }
                  break;
              }
            } else {
              fputc(curr, recv_file);
              --chunklen;
            }
          }
        }
      }
    }

closedown: fclose(recv_file);
  }

  close_cli();
  return 0;
}  

/**
 * Parse a HTTP 1.1 response. 
 *
 * @param[in] req The text of the response to parse.
 * @param[in] res_type The expected resource type.
 * @param[out] is_chunked A pointer to a boolean that will be set to true if 
 * the response is delivering the file in chunks.
 * @return True if request successfully parsed, false otherwise.
 */
bool parse_resp(char *resp, char *res_type, bool *is_chunked) {
  int respcode;
  char msgbuf[FIELD_BUFLEN + 1];
  char verbuf[FIELD_BUFLEN + 1];
  int linelen;

  char *end = strstr(resp, "\r\n\r\n");
  if (end == NULL) {
    fprintf(stderr, "No header terminator!\n");
    return false;
  }

  if (sscanf(resp, "%" FIELD_BUFLEN_STR "s %d %" FIELD_BUFLEN_STR "[^\r\n]\r\n%n", 
        verbuf, &respcode, msgbuf, &linelen) != 3) {
    fprintf(stderr, "Invalid response format!\n");
    return false;
  }

  if (strcmp(verbuf, "HTTP/1.1") != 0) {
    fprintf(stderr, "Unsupported HTTP version!\n");
    return false;
  }

  if (respcode != HTTP_OK) {
    printf("File not downloaded: %d %s\n", respcode, msgbuf);
    return false;
  }

  printf("Received HTTP response:\n%s\n", resp);

  resp += linelen;

  // start parsing header lines
  char hdrbuf[FIELD_BUFLEN], valbuf[DATA_BUFLEN];
  while (resp < end) {
    if (sscanf(resp, " %" FIELD_BUFLEN_STR "[^ :\r\n]: %" DATA_BUFLEN_STR 
          "[^\r\n] \r\n%n", hdrbuf, valbuf, &linelen) != 2) {
      fprintf(stderr, "Invalid header line: %s\n", resp);
      return false;
    }
    resp += linelen;
    debug_print("Processing %s...\n", hdrbuf);

    // lowercase field names for comparison
    char *p;
    for (p = hdrbuf; *p != 0; ++p) {
      *p = tolower(*p);
    }

    // check various supported headers
    if (strcmp(hdrbuf, "content-type") == 0) {
      if (strcmp(valbuf, res_type) != 0) {
        fprintf(stderr, "Incorrect content type, expected %s, got %s", res_type, valbuf);
        return false;
      }
    } else if (strcmp(hdrbuf, "transfer-encoding") == 0) {
      if (strcmp(valbuf, "chunked") == 0) {
        *is_chunked = true;
      }
    }
  }
  debug_print("Header is valid.\n");
  return true;
}

FILE *setup_get_resource(struct reqinfo *info) {
  static char buf[FIELD_BUFLEN] = USR;
  char *bufptr = buf + strlen(USR);
  strcpy(bufptr, info->url);
  FILE *temp = fopen(buf, "r+b");
  if (temp != NULL) {
    return temp;
  } else {
    fclose(temp);
    return fopen(buf, "wb");
  }
}

size_t write_get_req(char *req, struct reqinfo *info) {
  char *reqcurr;
  char *conntype;
  if (info->keep_alive) {
    conntype = "keep-alive";
  } else {
    conntype = "close";
  }

  char timestamp[HTTP_TIME_LEN + 1];
  struct tm tm = *gmtime(&info->if_mod_since_time);
  strftime(timestamp, HTTP_TIME_LEN + 1, "%a, %d %b %Y %H:%M:%S GMT", &tm);

  reqcurr = _strcpy(req, "GET ");
  append(reqcurr, info->url);
  append(reqcurr, " HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Accept: ");
  append(reqcurr, info->accept);
  append(reqcurr, "\r\n"
      "Connection: ");
  append(reqcurr, conntype);
  append(reqcurr, "\r\n");

  if (info->if_mod_since) {
    append(reqcurr, "If-Modified-Since: ");
    append(reqcurr, timestamp);
    append(reqcurr, "\r\n");
  }

  append(reqcurr, "\r\n");
  return reqcurr - req;
}

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

/**
 * strcpy that returns pointer to null byte of concatenated strings.
 */
char *_strcpy(char *destination, const char *source) {
  char s = *source;
  while (s != 0) {
    *(destination++) = s;
    s = *(++source);
  }
  *destination = '\0';
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
