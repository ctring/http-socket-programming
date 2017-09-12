#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#define HTTP_SCHEME "http://"
#define HTTPS_SCHEME "https://"

struct timeval savedTime;

void print_buffer(const char* name, const char* buffer)
{
    printf("============ BEGIN %s ============\n", name);
    printf("%s\n", buffer);
    printf("============ END %s ============\n", name);
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int is_prefix(const char* pat, const char* str)
{
    size_t patLen = strlen(pat);
    size_t strLen = strlen(str);
    return strLen >= patLen && strncmp(pat, str, patLen) == 0;
}

 void parse_uri(const char *uri, char **host, char **path)
 {
     int offsetHost = 0;
     int lenHost;
     char *s;
     if (is_prefix(HTTPS_SCHEME, uri))
     {
         offsetHost += strlen(HTTPS_SCHEME);
     }
     else if (is_prefix(HTTP_SCHEME, uri))
     {
         offsetHost += strlen(HTTP_SCHEME);
     }
 
     if ((s = strchr(uri + offsetHost, '/')) == NULL)
     {
         *host = malloc(strlen(uri) - offsetHost + 2);
         *path = malloc(2);
         strcpy(*host, uri + offsetHost);
         strcpy(*path, "/");
     }
     else 
     {
         lenHost = s - (uri + offsetHost);
         *host = malloc(lenHost + 1);
         *path = malloc(strlen(uri) - (s - uri) + 1);
         strncpy(*host, uri + offsetHost, lenHost);
         (*host)[lenHost] = '\0';
         strcpy(*path, s);
     }
 }

int get_header_value(const char *headers, const char *field, char *buf, size_t sz)
{
    size_t fieldLen = strlen(field);
    size_t bufLen;
    char *fieldName = (char*)malloc(fieldLen + 2);
    char *pch, *nextCRLF;

    if (sz <= 0) return 0;

    strncpy(fieldName, field, fieldLen);
    fieldName[fieldLen]     = ':';
    fieldName[fieldLen + 1] = '\0';

    buf[0] = '\0';    
    if ((pch = strstr(headers, fieldName)) != NULL)
    {
        pch += fieldLen + 1;
        while (*pch == ' ' || *pch =='\t') {
            pch++;
        }
        nextCRLF = strstr(pch, CRLF);
        bufLen = min(sz - 1, nextCRLF - pch) + 1;
        strncpy(buf, pch, bufLen - 1);
        buf[bufLen - 1] = '\0';
    }

    free(fieldName);
    return buf[0] != '\0';
}

void start_timer()
{
    gettimeofday(&savedTime, NULL);
}

double end_timer()
{
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    double diff_s = currentTime.tv_sec - savedTime.tv_sec;
    double diff_us = currentTime.tv_usec - savedTime.tv_usec;
    return diff_s * 1000 + diff_us / 1000;
}
