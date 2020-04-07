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
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <assert.h>

#define DEBUG 1

#define WT_INFO(MSG, CODE) fprintf(stderr, "%s Error code: %d\n", MSG, CODE)
#define WT_DIE(MSG, CODE) WT_INFO(MSG, CODE); close_serv(); exit(1)
#define WT_DISCONN(SOCK) shutdown(SOCK, SD_BOTH); closesocket(SOCK); break 
#define WT_SENDERR(ERR, SOCK) if (send_err_resp(ERR)) { continue; } else { WT_DISCONN(SOCK); }

#define debug_print(...) do { if (DEBUG) fprintf(stderr, __VA_ARGS__); } while (0)
#define append(str1, str2) str1 = _strcpy(str1, str2)

#define DEFAULT_PORT 33333
#define DATA_BUFLEN 1024
#define DATA_BUFLEN_STR "1024"
#define ADDR_BUFLEN 32
#define FIELD_BUFLEN 128
#define FIELD_BUFLEN_STR "128"
#define HEXSTR_MAXLEN 64
#define BACKLOG 5

#define SRV "srv"
#define SRV_LEN 3
#define FILE_CNT 3

// some Windows macro I'm not using
#ifdef DELETE
#undef DELETE
#endif
#define HTTP_BAD_REQ 400
#define HTTP_NOT_FOUND 404
#define HTTP_WRONG_MTD 405
#define HTTP_NOT_ACC 406
#define HTTP_TOO_LARGE 431
#define HTTP_WRONG_VER 505
#define HTTP_OK 200
#define HTTP_NOT_MOD 304
#define HTTP_TIME_LEN 29

enum http_mtd {GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH,
  MTD_COUNT}; // for keeping track of number of methods
const char *http_mtd_strs[] = {"GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT",
  "OPTIONS", "TRACE", "PATCH"};
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

BOOL WINAPI int_handler(DWORD sig_type);
void close_serv();
FILE *locate(char *url, char *accept, int *errcode, const char **type);
bool parse_timestamp(char *timestamp, struct tm *timestruct);
bool parse_req(const char *req, struct reqinfo *info, int *errcode);
bool send_err_resp(int errcode);
char *write_date_hdr(char *buf);
void write_ok_resp(char *resp, struct reqinfo *info, struct stat *file_status,
    const char *type);
bool handle_get(char *sendbuf, struct reqinfo *info, struct stat *file_status, 
    char *resp);
char *_strcat(char *destination, const char *source);
char *_strcpy(char *destination, const char *source);

SOCKET listen_sock = INVALID_SOCKET; // for receiving connections
SOCKET conn_sock = INVALID_SOCKET; // for maintaining a connection
FILE *serv_file = NULL;

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
    WT_DIE("Failed to create listening socket!", WSAGetLastError());
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
    WT_DIE("Failed to bind listening socket!", WSAGetLastError());
  }

  // Start listening
  if (listen(listen_sock, BACKLOG) < 0) {
    WT_DIE("Failed to start listening!", WSAGetLastError());
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

    // Read in data
    int recv_status = SOCKET_ERROR;
    char recvbuf[DATA_BUFLEN + 1];
    char sendbuf[DATA_BUFLEN + 1];

    // Receive from socket
    while (1) {
      recv_status = recv(conn_sock, recvbuf, DATA_BUFLEN, 0);
      if (recv_status == SOCKET_ERROR) {
        WT_INFO("Error receiving data!", WSAGetLastError());
        continue;
      } else if (recv_status == 0) {
        printf("Connection closed by client.\n");
        break;
      } 

      printf("Read %d bytes.\n", recv_status);
      recvbuf[recv_status] = '\0';

      debug_print("Data received: %s\n", recvbuf);

      struct reqinfo info;
      int errcode = 0;
      const char *type;

      // parse request header
      if (!parse_req(recvbuf, &info, &errcode)) {
        WT_SENDERR(errcode, conn_sock);
      }

      serv_file = locate(info.url, info.accept, &errcode, &type);
      struct stat file_status;
      fstat(fileno(serv_file), &file_status);

      if (serv_file == NULL) {
        if (!info.keep_alive) {
          send_err_resp(errcode);
          WT_DISCONN(conn_sock);
        } else {
          WT_SENDERR(errcode, conn_sock);
        }
      }

      // check last modified
      if (info.if_mod_since) {
        if (file_status.st_mtime <= info.if_mod_since_time) {
          fclose(serv_file);
          errcode = HTTP_NOT_MOD;
          if (!info.keep_alive) {
            send_err_resp(errcode);
            WT_DISCONN(conn_sock);
          } else {
            WT_SENDERR(errcode, conn_sock);
          }
        }
      }

      debug_print("Resource retrieved.\n");

      char resp[DATA_BUFLEN]; 
      write_ok_resp(resp, &info, &file_status, type);

      // only implementing GET for this assignment, and all transfers are
      // automatically chunked
      // format is <chunk size in hex>\r\n<chunk>\r\n
      if (info.mtd == GET) {
        if (!handle_get(sendbuf, &info, &file_status, resp)) {
          fclose(serv_file);
          WT_INFO("Failed to send while handling GET!", WSAGetLastError());
          WT_DISCONN(conn_sock);
        }
      } else {
        fclose(serv_file);
        errcode = HTTP_WRONG_MTD;
        WT_SENDERR(errcode, conn_sock);
      }


      fclose(serv_file);

      if (!info.keep_alive) {
        printf("Connection completed and closed.\n");
        WT_DISCONN(conn_sock);
      }
    } 
  }
  return 0;
}  

/**
 * Parse a HTTP 1.1 request. 
 *
 * @param[in] req The text of the request to parse.
 * @param[out] info A struct holding the supported information contained in the
 * request.
 * @param[out] errcode Pointer to the error code to set on encountering an
 * error.
 */
bool parse_req(const char *req, struct reqinfo *info, int *errcode) {

  char mtdbuf[FIELD_BUFLEN + 1], verbuf[FIELD_BUFLEN + 1]; 
  int linelen; 

  // check validity and mark start of data
  char *end = strstr(req, "\r\n\r\n");
  if (end == NULL) {
    debug_print("No header terminator!\n");
    *errcode = HTTP_BAD_REQ;
    return false;
  }
  info->data = end + strlen("\r\n\r\n");

  // parse first line
  if (sscanf(req, "%" FIELD_BUFLEN_STR "s %" FIELD_BUFLEN_STR "s %" 
        FIELD_BUFLEN_STR "s \r\n%n", mtdbuf, info->url, verbuf, &linelen) != 3) {
    debug_print("Wrong request line!\n");
    *errcode = HTTP_BAD_REQ;
    return false;
  }
  req += linelen;

  // identify type of request
  // can be made marginally more efficient by keying the methods on length
  int i;
  for (i = 0; i < (int) MTD_COUNT; ++i) {
    if (strcmp(mtdbuf, http_mtd_strs[i]) == 0) {
      info->mtd = (enum http_mtd) i;
      break;
    }
  }
  if (i == (int) MTD_COUNT) {
    *errcode = HTTP_WRONG_MTD;
    return NULL;
  }

  // check HTTP version
  // only supporting HTTP/1.1
  if (strcmp(verbuf, "HTTP/1.1") != 0) {
    *errcode = HTTP_WRONG_VER;
    return NULL;
  }

  // read header lines
  // only supporting Accept, If-Modified-Since and Connection
  char hdrbuf[FIELD_BUFLEN], valbuf[DATA_BUFLEN];

  // initialise info structure
  info->accept[0] = '\0';
  info->keep_alive = true;

  while (req < end) {
    if (sscanf(req, " %" FIELD_BUFLEN_STR "[^ :\r\n]: %" DATA_BUFLEN_STR 
          "[^\r\n] \r\n%n", hdrbuf, valbuf, &linelen) != 2) {
      debug_print("Invalid header line: %s\n", req);
      *errcode = HTTP_BAD_REQ;
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
      strcpy(info->accept, valbuf);
    } else if (strcmp(hdrbuf, "if-modified-since") == 0) {
      struct tm mod;
      if (!parse_timestamp(valbuf, &mod)) {
        debug_print("Invalid timestamp: %s\n", valbuf);
        *errcode = HTTP_BAD_REQ;
        return false;
      }
      info->if_mod_since = true;
      info->if_mod_since_time = mktime(&mod);
    } else if (strcmp(hdrbuf, "connection") == 0) {
      if (strcmp(valbuf, "keep-alive") == 0) {
        info->keep_alive = true;
      } else if (strcmp(valbuf, "close") == 0) {
        info->keep_alive = false;
      } else {
        debug_print("Invalid connection type: %s\n", valbuf);
        *errcode = HTTP_BAD_REQ;
        return false;
      }
    }
  }
  debug_print("Header is valid.\n");
  return true;    
}

/**
 * Handles a GET request.
 */
bool handle_get(char *sendbuf, struct reqinfo *info, struct stat *file_status, 
    char *resp) {
  size_t full_chunk_size = DATA_BUFLEN - HEXSTR_MAXLEN - 4;
  size_t full_chunks = file_status->st_size / full_chunk_size;
  size_t last_chunk_size = file_status->st_size % full_chunk_size;

  char *sendbufcurr, *sendbufend;
  int send_status = SOCKET_ERROR;

  // response will have Date, Last-Modified, Connection,
  // Transfer-Encoding and Content-Type
  // NOTE: this code will result in buffer overflows if DATA_BUFLEN is set
  // too low

  append(resp, "Transfer-Encoding: chunked\r\n");
  append(resp, "\r\n");
  send_status = send(conn_sock, resp, strlen(resp), 0); 
  if (send_status < 0) {
    return false;
  }
  debug_print("HTTP response header: %s\n", resp);

  char chunklen_hexstr[HEXSTR_MAXLEN + 2 + 1];
  sprintf(chunklen_hexstr, "%" LL_FMT "X\r\n", full_chunk_size);
  size_t full_hexstr_len = strlen(chunklen_hexstr);

  debug_print("Writing out chunks...\n");

  // write full chunks
  if (full_chunks > 0) {
    sendbufcurr = _strcpy(sendbuf, chunklen_hexstr);
    size_t c;
    for (c = 0; c < full_chunks; ++c) {
      sendbufend = sendbufcurr + fread(sendbufcurr, 
          1, full_chunk_size, serv_file);
      strcpy(sendbufend, "\r\n");
      send_status = send(conn_sock, sendbuf, 
          full_hexstr_len + full_chunk_size + 2, 0);
      if (send_status < 0) {
        return false;
      }
      //debug_print("Sent %d-byte full chunk: %s\n", send_status, sendbuf);
    }
  }

  // write last chunk
  sprintf(chunklen_hexstr, "%" LL_FMT "X\r\n", last_chunk_size);
  size_t last_hexstr_len = strlen(chunklen_hexstr);
  sendbufcurr = _strcpy(sendbuf, chunklen_hexstr);
  sendbufend = sendbufcurr + fread(sendbufcurr, 1, last_chunk_size, 
      serv_file);
  strcpy(sendbufend, "\r\n");
  send_status = send(conn_sock, sendbuf, 
      last_hexstr_len + last_chunk_size + 2, 0);
  if (send_status < 0) {
    return false;
  }
  //debug_print("Sent %d-byte last chunk: %s\n", send_status, sendbuf);

  // signal end of chunks
  const char *chunk_end = "0\r\n\r\n";
  send_status = send(conn_sock, chunk_end, strlen(chunk_end), 0);
  if (send_status < 0) {
    return false;
  }
  //debug_print("Sent end of chunks: %s\n", chunk_end);

  return true;
}

/**
 * Create a generic OK response usable for any method. Note that this function
 * may overflow the resp buffer if DATA_BUFLEN is too small.
 */
void write_ok_resp(char *resp, struct reqinfo *info, struct stat *file_status, 
    const char *type) {
  resp = write_date_hdr(_strcpy(resp, "HTTP/1.1 200 OK\r\n"));
  append(resp, "Last-Modified: ");
  struct tm tm = *gmtime(&file_status->st_mtime);
  strftime(resp, HTTP_TIME_LEN + 1, "%a, %d %b %Y %H:%M:%S GMT", &tm);
  resp = _strcpy(resp + HTTP_TIME_LEN, "\r\n");
  if (info->keep_alive) {
    append(resp, "Connection: keep-alive\r\n");
  } else {
    append(resp, "Connection: close\r\n");
  }
  append(resp, "Content-Type: ");
  append(resp, type);
  append(resp, "\r\n");
}

/**
 * Locates a resource, returning its file pointer and MIME type.
 */
FILE *locate(char *url, char *accept, int *errcode, const char **type) {
  debug_print("Finding file %s...\n", url);
  char path[FIELD_BUFLEN + SRV_LEN + 1] = SRV;

  int i;
  for (i = 0; i < FILE_CNT; ++i) {
    if (strcmp(url, filenames[i]) == 0) {
      if ((accept[0] != 0) && (strcmp(accept, mime[i]) != 0)) {
        *errcode = HTTP_NOT_ACC;
        return NULL;
      } else {
        *type = mime[i];
        strcpy(path + SRV_LEN, filenames[i]);
        return fopen(path, "rb");
      }
    }
  }

  *errcode = HTTP_NOT_FOUND;
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
 * Send an error response to the client. Returns true if response was
 * successfully sent, false otherwise.
 */
bool send_err_resp(int errcode) {
  char closebuf[DATA_BUFLEN] = "HTTP/1.1 ";
  char *bufptr = closebuf;
  char *errstr;
  switch(errcode) {
    case HTTP_NOT_FOUND: 
      errstr = "404 Not Found";
      break;
    case HTTP_WRONG_MTD: 
      errstr = "405 Method Not Allowed";
      break;
    case HTTP_NOT_ACC: 
      errstr = "406 Not Acceptable";
      break;
    case HTTP_TOO_LARGE: 
      errstr = "413 Payload Too Large";
      break;
    case HTTP_WRONG_VER: 
      errstr = "505 HTTP Version Not Supported";
      break;
    case HTTP_NOT_MOD: 
      errstr = "304 Not Modified";
      break;
    case HTTP_BAD_REQ:
    default:
      errstr = "400 Bad Request";
      break;
  }
  bufptr = _strcat(closebuf, errstr);
  append(bufptr, "\r\n");
  strcpy(write_date_hdr(bufptr), "\r\n");
  if (send(conn_sock, closebuf, strlen(closebuf), 0) < 0) {
    WT_INFO("Failed to send data!", WSAGetLastError());
    return false;
  }
  debug_print("Sending %s error to client...\n", errstr);
  return true;
}

/**
 * Close down server connections and clean up.
 */
void close_serv() {
  if (serv_file) {
    fclose(serv_file);
  }
  printf("\nClosing sockets.\n");
  shutdown(listen_sock, SD_BOTH);
  shutdown(conn_sock, SD_BOTH);
  closesocket(listen_sock);
  closesocket(conn_sock);
  WSACleanup();
}

/**
 * Writes the Date header to the position pointed to by buf. buf must have at
 * least 46 bytes of free space in it.
 */
char *write_date_hdr(char *buf) {
  buf = _strcpy(buf, "Date: ");
  time_t now = time(0);
  struct tm tm = *gmtime(&now);
  strftime(buf, HTTP_TIME_LEN + 1, "%a, %d %b %Y %H:%M:%S GMT", &tm);
  return _strcpy(buf + HTTP_TIME_LEN, "\r\n");
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
