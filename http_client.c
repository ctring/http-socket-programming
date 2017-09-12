#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

#define REQUEST_SIZE (1024 * 4)
#define BUFFER_SIZE (1024 * 4)

int printRTT = 0;

void print_usage() 
{
    eprintf("usage: http_client [-p] server_url port_number\n");
    eprintf("\t-p prints the RTT\n");
}

/**
 * Gets the status code in the HTTP response status line
 */
int get_status_code(char* statusLine)
{
    int code = 0;
    sscanf(statusLine, "%*s %d %*s", &code);
    return code;
}

int open_socket_and_connect(const char *serverName, const char *portNumber)
{
    int ecode;
    struct addrinfo hints, *servInfo, *p;
    int sockfd;
    char ipAddress[INET6_ADDRSTRLEN];
    double rtt = 0;

    // Get host information
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if ((ecode = getaddrinfo(serverName, portNumber, &hints, &servInfo)) != 0)
    {
        eprintf("getaddrinfo: %s\n", gai_strerror(ecode));
        return -1;
    }

    // Try to connect to the first working address spec
    for (p = servInfo; p != NULL; p = p->ai_next) 
    {
        inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
                  ipAddress, sizeof ipAddress);
    
        printf("client: connecting to %s\n", ipAddress);
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("client: socket");
            continue;
        }
        start_timer();
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("client: socket");
            continue;
        }
        rtt = end_timer();
        break;
    }

    if (p == NULL)
    {
        eprintf("client: failed to connect\n");
        return -1;
    }

    // Get readable IP address
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), ipAddress, sizeof ipAddress);
    printf("client: connected to %s\n", ipAddress);

    if (printRTT) {
        printf("Round-trip time = %.2f ms\n", rtt);
    }

    freeaddrinfo(servInfo);
    return sockfd;
}

int send_get_request(int sockfd, const char *host, const char *path)
{
    char request[REQUEST_SIZE];
    int sent = 0;

    snprintf(request, REQUEST_SIZE, 
        "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n",
        path, host);

    if ((sent = send(sockfd, request, strlen(request), 0)) < 0)
    {
        perror("client: send");
        return -1;
    }

    return sent;
}

int recv_http_response(int sockfd, char **status, char **headers, char **body)
{   
    int LEN_CRLF = strlen(CRLF);
    int LEN_CRLFCRLF = strlen(CRLFCRLF);

    int filling = 0;            // 0: status ; 1: headers; 2: body
    int bytesRcvd;
    int totalBytesRcvd = 0;
    int totalBodyBytesRcvd = 0;
    int contentLength = -1;
    int transferEncodingChunked = 0;
    int statusCode;
    int noBody = 0;
    int nextBytes;
    int stop = 0;

    char buffer[BUFFER_SIZE];
    char *bodyChunks = NULL, *bodyChunksPtr, *bodyChunksEnd;
    char *pch;
    
    *status   = (char *)malloc(max(STATUS_LINE_SIZE, BUFFER_SIZE));
    *headers  = (char *)malloc(max(HEADER_SIZE, BUFFER_SIZE));
    *body     = (char *)malloc(max(BODY_SIZE, BUFFER_SIZE));

    *status[0] = *headers[0] = *body[0] = '\0';

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
            strcat(*status, buffer);
        }
        else if (filling == 1) // Filling headers buffer
        {
            strcat(*headers, buffer);
        }
        else if (filling == 2) // Filling body buffer
        {
            if (transferEncodingChunked)
            {
                strcat(bodyChunks, buffer);
            }
            else {
                totalBodyBytesRcvd += bytesRcvd;
                strcat(*body, buffer);
            }
        }

        if (filling == 0)
        {
            // If CRLF is found on status line, cut it off from there and move
            // the cuf-off part to the headers buffer
            if ((pch = strstr(*status, CRLF)) != NULL)
            {
                pch[0] = '\0';
                strcat(*headers, pch + LEN_CRLF);           
               
                statusCode = get_status_code(*status);
                noBody = statusCode == 204 || statusCode == 304 || statusCode / 100 == 1;

                // Finished with filling status line, move on to headers
                filling = 1;                 
            }
        }
        if (filling == 1)
        {
            // If 2 consecutive CRLFs are found in header section, cut if off at between
            // the 2 CRLFs and move the cut-off (without any CRLF) to the body buffer
            if ((pch = strstr(*headers, CRLFCRLF)) != NULL)
            {
                pch[LEN_CRLF] = '\0';
                // Some status code must not have body part
                if (noBody) {
                    break;
                }
                // Parse header and look for Transfer-Encoding or Content-Length
                if (get_header_value(*headers, "Transfer-Encoding", buffer, BUFFER_SIZE))
                {
                    if ((transferEncodingChunked = (strcmp(buffer, "chunked") == 0)))
                    {
                        bodyChunks = (char *)malloc(BODY_SIZE);
                        bodyChunks[0] = '\0';
                        strcat(bodyChunks, pch + LEN_CRLFCRLF);
                        bodyChunksPtr = bodyChunks;    
                    }
                }
                else if (get_header_value(*headers, "Content-Length", buffer, BUFFER_SIZE))
                {
                    contentLength = atoi(buffer);
                    strcat(*body, pch + LEN_CRLFCRLF);   
                    totalBodyBytesRcvd = strlen(*body);                    
                }
                else {
                    strcat(*body, pch + LEN_CRLFCRLF);            
                }

                // Move on to body
                filling = 2;
            }
        }
        if (filling == 2)
        {
            if (transferEncodingChunked)
            {
                bodyChunksEnd = bodyChunksPtr + strlen(bodyChunksPtr);
                while (1) 
                {
                    // Get the size of the next chunk
                    nextBytes = strtol(bodyChunksPtr, &pch, 16);
                    
                    if (nextBytes == 0) {
                        stop = 1;
                        break;
                    }
                    if (pch[0] == '\r' && pch[1] == '\n' 
                        && (bodyChunksEnd - pch) >= nextBytes) 
                    {
                        pch += 2;
                        strncat(*body, pch, nextBytes);
                        bodyChunksPtr = pch + nextBytes;
                    }
                    else {
                        break;
                    }
                }
                if (stop) {
                    free(bodyChunks);
                    break;
                }
            }
            else if (contentLength >= 0)
            {
                if (totalBodyBytesRcvd >= contentLength) break;
            }
        }
    }
    
    return totalBytesRcvd;
}

#ifndef TEST

int main(int argc, char *argv[])
{
    int i;
    char *host, *path;

    int sockfd;    
    int bytesRcvd;
    FILE *bodyFile;
    char *statusLine, *headers, *body;

    // Parse the arguments
    if (argc <= 2) {
        print_usage();
        return 1;
    }
    for (i = 1; i < argc - 2; i++)
    {
        if (strcmp("-p", argv[i]) == 0) {
            printRTT = 1;
        }
        else {
            eprintf("Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    parse_uri(argv[argc - 2], &host, &path);

    if ((sockfd = open_socket_and_connect(host, argv[argc - 1])) < 0)
    {
        return 1;
    }

    if (send_get_request(sockfd, host, path) < 0)
    {
        return 1;
    }

    bytesRcvd = recv_http_response(sockfd, &statusLine, &headers, &body);

    if (bytesRcvd > 0)
    {
        printf("\n%s\n%s\n", statusLine, headers);
        printf("--------------\n");
        if (body[0] == '\0')
        {
            printf("Body is empty.\n\n");
        }
        else {
            bodyFile = fopen("body.html", "w");
            fprintf(bodyFile, "%s", body);
            fclose(bodyFile);
            printf("Body file saved to body.html\n\n");
        }
    }
    else {
        printf("client: received nothing from server or error occured\n");
    }

    free(statusLine);
    free(headers);
    free(body);

    close(sockfd);

    return 0;
}

#else

int main(int argc, char *argv[])
{
    char *ptr1, *ptr2;

    check("Test get_status_code 1", get_status_code("HTTP/1.1 200 OK") == 200);
    check("Test get_status_code 2", get_status_code("HTTP/1.1 300 OK") == 300);
    check("Test get_status_code 3", get_status_code("  HTTP/1.0  2000  OK") == 2000);
    
    parse_uri("http://google.com/test", &ptr1, &ptr2);
    check("Test parse_uri (host)", strcmp(ptr1, "google.com") == 0);
    check("Test parse_uri (path)", strcmp(ptr2, "/test") == 0);
    free(ptr1);
    free(ptr2);

    ptr1 = (char*)malloc(100);
    get_header_value("field1: xxx\r\nfield2:   yyyy", "field1", ptr1, 100);
    check("Test get_header_value", strcmp(ptr1, "xxx") == 0);
    get_header_value("field1: xxx\r\nfield2:   yyyy", "field2", ptr1, 100);
    check("Test get_header_value", strcmp(ptr1, "yyyy") == 0);
    free(ptr1);

    return 0;
}

#endif
