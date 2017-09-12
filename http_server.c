#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "utils.h"

#define BUFFER_SIZE (1024 * 4)
#define NUM_THREADS 10
#define BACKLOG 20

void print_usage() 
{
    eprintf("usage: http_server port_number\n");
}

void sigterm_handler(int signum)
{
    printf("Server stopped!\n");
    exit(1);
}

int open_socket_and_listen(const char *portNumber)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int yes = 1;    
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, portNumber, &hints, &servinfo)) != 0) {
        eprintf("getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) {
            perror("setsockopt");
            return -1;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) < 0) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }
    freeaddrinfo(servinfo);
    if (p == NULL) {
        eprintf("server: failed to bind\n");
        return -1;
    }
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        return -1;
    }
    return sockfd;
}

int recv_http_request(int sockfd, char **request, char **header)
{
    int LEN_CRLF = strlen(CRLF);
    int totalBytesRcvd = 0, bytesRcvd;
    char buffer[BUFFER_SIZE];
    char *pch;

    int filling = 0;            // 0: status ; 1: header;
    
    *request = (char*)malloc(max(REQUEST_LINE_SIZE, BUFFER_SIZE));
    *header  = (char*)malloc(max(HEADER_SIZE, BUFFER_SIZE));

    *request[0] = *header[0] = '\0';

    while (1)
    {
        bytesRcvd = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        buffer[bytesRcvd] = '\0';

        if (bytesRcvd <= 0) {
            if (bytesRcvd < 0) {
                totalBytesRcvd = bytesRcvd;
            }
            break;
        }
        totalBytesRcvd += bytesRcvd;
        
        if (filling == 0)     // Filling status line buffer
        {
            strcat(*request, buffer);
        }
        else if (filling == 1) // Filling header buffer
        {
            strcat(*header, buffer);
        }

        if (filling == 0)
        {
            // If CRLF is found on status line, cut it off from there and move
            // the cuf-off part to the header buffer
            if ((pch = strstr(*request, CRLF)) != NULL)
            {
                pch[0] = '\0';
                strcat(*header, pch + LEN_CRLF);           

                // Finished with filling status line, move on to header
                filling = 1;                 
            }
        }
        if (filling == 1)
        {
            // If 2 consecutive CRLFs are found in header section, cut if off at between
            // the 2 CRLFs and move the cut-off (without any CRLF) to the body buffer
            if ((pch = strstr(*header, CRLFCRLF)) != NULL)
            {
                pch[LEN_CRLF] = '\0';
                break;
            }
        }
    }
    return totalBytesRcvd;
}

void get_status_line(int statusCode, char *status, int sz)
{
    char format[] = "HTTP/1.1 %d %s\r\n";
    if (statusCode == 405)
    {
        snprintf(status, sz, format, statusCode, "Method Not Allowed");
    }
    else if (statusCode == 505)
    {
        snprintf(status, sz, format, statusCode, "HTTP Version Not Supported");
    }
    else if (statusCode == 200)
    {
        snprintf(status, sz, format, statusCode, "OK");
    }
    else if (statusCode == 404)
    {
        snprintf(status, sz, format, statusCode, "Not Found");
    }
}

int send_http_response(int sockfd, const char *request, const char *reqHeader)
{
    char method[5], uri[URI_SIZE], httpVersion[16];
    char buffer[BUFFER_SIZE], resHeader[HEADER_SIZE];
    
    char *body = NULL;
    int bodyLength = 0;
    int statusCode;
    int bytesRead;
    int bytesSent, totalBytesSent = 0;
    char status[STATUS_LINE_SIZE];
    FILE *file;
    
    resHeader[0] = '\0';

    sscanf(request, "%s %s %s", method, uri, httpVersion);
    if (strcmp("GET", method) != 0)
    {
        statusCode = 405;
    }
    else if (strcmp("HTTP/1.1", httpVersion) != 0)
    {
        statusCode = 505;
    }
    else {
        file = fopen(uri + 1, "rb");
        if (file) {
            body = (char*)malloc(BODY_SIZE);
            do {
                bytesRead = fread(buffer, 1, BUFFER_SIZE, file);
                memcpy(body + bodyLength, buffer, bytesRead);
                bodyLength += bytesRead;
            } while (bytesRead >= BUFFER_SIZE);
            fclose(file);
            statusCode = 200;
        }
        else {
            statusCode = 404;
        }
    }

    // Send status line
    get_status_line(statusCode, status, STATUS_LINE_SIZE);
    if ((bytesSent = send(sockfd, status, strlen(status), 0)) < 0)
    {
        free(body);
        return -1;
    }
    totalBytesSent += bytesSent;
    
    // Send header
    if (body != NULL) {
        snprintf(resHeader + strlen(resHeader), HEADER_SIZE, 
                 "Content-Length: %d\r\n", bodyLength);
       
    }

    strcat(resHeader, "\r\n");

    if ((bytesSent = send(sockfd, resHeader, strlen(resHeader), 0)) < 0)
    {
        free(body);
        return -1;
    }
    totalBytesSent += bytesSent;

    // Send body
    if ((bytesSent = send(sockfd, body, bodyLength, 0)) < 0)
    {
        free(body);
        return -1;
    }
    totalBytesSent += bytesSent;
    free(body);
    return totalBytesSent;
}

sem_t sem;

void* handle_connection(void *argument)
{
    int* sockfdPtr = (int*)argument;
    int sockfd = *sockfdPtr;
    int bytesRcvd;
    char *request = NULL;
    char *header = NULL;

    if ((bytesRcvd = recv_http_request(sockfd, &request, &header)) < 0)
    {
        eprintf("server: error occured while receiving http request\n");
    }
    else {
        printf("server: got request - %s\n", request);
        send_http_response(sockfd, request, header);
    }
   
    free(request);
    free(header);
    free(sockfdPtr);
    close(sockfd);
    sem_post(&sem);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t thread;
    
    int sockfd;
    int *newfd;
    struct sockaddr_storage clientAddr;    
    socklen_t sin_size;
    struct sigaction sa;
    char s[INET6_ADDRSTRLEN];

    if (argc != 2) {
        print_usage();
        return 1;
    }

    if ((sockfd = open_socket_and_listen(argv[1])) < 0)
    {
        return 1;
    }

    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connection...\n");
    sem_init(&sem, 0, NUM_THREADS);
    while (1) {
        sin_size = sizeof clientAddr;
        newfd = (int*)malloc(sizeof(int));
        *newfd = accept(sockfd, (struct sockaddr *)&clientAddr, &sin_size);
        if (newfd < 0) {
            perror("accept");
            continue;
        }

        sem_wait(&sem);
        inet_ntop(clientAddr.ss_family,
            get_in_addr((struct sockaddr *)&clientAddr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);
        
        pthread_create(&thread, NULL, handle_connection, newfd);
    }
    return 0;
}
