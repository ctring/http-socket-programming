#ifndef UTILS_H
#define UTILS_H

#include <netinet/in.h> 

#define HEADER_SIZE (1024 * 8)
#define BODY_SIZE (10 * 1024 * 1024)
#define REQUEST_LINE_SIZE (1024 * 4)
#define STATUS_LINE_SIZE (1024 * 4)
#define URI_SIZE (256)

#define CRLF "\r\n"
#define CRLFCRLF "\r\n\r\n"

#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define check(l, e) { printf(l); assert(e); printf(" OK\n"); }
#define min(a, b) ((a) < (b) ? a : b)
#define max(a, b) ((a) > (b) ? a : b)


void *get_in_addr(struct sockaddr *sa);
int is_prefix(const char *pat, const char *str);
void parse_uri(const char *uri, char **host, char **path); 
int get_header_value(const char *headers, const char *field, char *buf, size_t sz);

void print_buffer(const char* name, const char* buffer);

void start_timer();
double end_timer();

#endif

