#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_BYTES 4096
#define MAX_CLIENTS 400

pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;

int port_number = 8080;
int proxy_socketId;

// ---------------- CACHE STRUCTURE ----------------
typedef struct cache_element cache_element;
struct cache_element {
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    cache_element* next;
};

cache_element* head = NULL;
int cache_size = 0;

// Forward declarations
cache_element* find(char* url);
int add_cache_element(char* data,int size,char* url);
void remove_cache_element();
int handle_request(int clientSocket, struct ParsedRequest* request, char* full_url);
int sendErrorMessage(int socket, int status_code);

// ---------------- HANDLE REQUEST ----------------
int handle_request(int clientSocket, struct ParsedRequest* request, char* full_url) {
    char* buf = (char*)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");

    size_t len = strlen(buf);

    if(ParsedHeader_set(request, "Connection", "close") < 0)
        printf("Set header failed\n");

    if(ParsedHeader_get(request, "Host") == NULL)
        ParsedHeader_set(request, "Host", request->host);

    if(ParsedRequest_unparse_headers(request, buf + len, MAX_BYTES - len) < 0)
        printf("Failed to unparse headers\n");

    int server_port = (request->port) ? atoi(request->port) : 80;

    // Connect to remote server
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket < 0) { perror("Socket creation failed"); free(buf); return -1; }

    struct hostent* host = gethostbyname(request->host);
    if(!host) { fprintf(stderr,"Host not found\n"); free(buf); return -1; }

    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    bcopy(host->h_addr, &server_addr.sin_addr.s_addr, host->h_length);

    if(connect(remoteSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        free(buf);
        return -1;
    }

    send(remoteSocket, buf, strlen(buf), 0);
    bzero(buf, MAX_BYTES);

    int bytes_recv = recv(remoteSocket, buf, MAX_BYTES-1, 0);
    char* temp_buffer = (char*)malloc(MAX_BYTES);
    int temp_index = 0;

    while(bytes_recv > 0) {
        send(clientSocket, buf, bytes_recv, 0);

        for(int i=0;i<bytes_recv;i++) temp_buffer[temp_index++] = buf[i];

        bzero(buf, MAX_BYTES);
        bytes_recv = recv(remoteSocket, buf, MAX_BYTES-1, 0);
    }
    temp_buffer[temp_index] = '\0';

    add_cache_element(temp_buffer, temp_index, full_url);

    free(buf);
    free(temp_buffer);
    close(remoteSocket);

    return 0;
}

// ---------------- THREAD FUNCTION ----------------
void* thread_fn(void* socketPtr) {
    sem_wait(&semaphore);

    int clientSocket = *(int*)socketPtr;

    char* buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    int bytes_received = recv(clientSocket, buffer, MAX_BYTES, 0);

    if(bytes_received <= 0) {
        free(buffer);
        close(clientSocket);
        sem_post(&semaphore);
        return NULL;
    }

    struct ParsedRequest* request = ParsedRequest_create();
    if(ParsedRequest_parse(request, buffer, bytes_received) >= 0) {
        printf("Method: %s\n", request->method);
        printf("Host: %s\n", request->host);
        printf("Path: %s\n", request->path);

        char full_url[1024];
        snprintf(full_url, sizeof(full_url), "%s%s", request->host, request->path);

        // Check cache
        cache_element* cached = find(full_url);
        if(cached) {
            printf("Cache HIT: Sending cached response for %s\n", full_url);
            send(clientSocket, cached->data, cached->len, 0);
        } else {
            printf("Cache MISS: Fetching from remote server for %s\n", full_url);
            handle_request(clientSocket, request, full_url);
        }

    } else {
        printf("Failed to parse request\n");
        sendErrorMessage(clientSocket, 400); // Bad request
    }

    ParsedRequest_destroy(request);
    free(buffer);
    close(clientSocket);
    sem_post(&semaphore);
    return NULL;
}

int sendErrorMessage(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);
    struct tm data = *gmtime(&now);
    strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

    switch(status_code)
    {
        case 400:
            snprintf(str,sizeof(str),
                "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: close\r\nContent-Type: text/html\r\nDate: %s\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD><BODY><H1>400 Bad Request</H1></BODY></HTML>",
                currentTime);
            break;

        case 403:
            snprintf(str,sizeof(str),
                "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nConnection: close\r\nContent-Type: text/html\r\nDate: %s\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD><BODY><H1>403 Forbidden</H1></BODY></HTML>",
                currentTime);
            break;

        case 404:
            snprintf(str,sizeof(str),
                "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nConnection: close\r\nContent-Type: text/html\r\nDate: %s\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD><BODY><H1>404 Not Found</H1></BODY></HTML>",
                currentTime);
            break;

        case 500:
            snprintf(str,sizeof(str),
                "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: close\r\nContent-Type: text/html\r\nDate: %s\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD><BODY><H1>500 Internal Server Error</H1></BODY></HTML>",
                currentTime);
            break;

        case 501:
            snprintf(str,sizeof(str),
                "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: close\r\nContent-Type: text/html\r\nDate: %s\r\n\r\n<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD><BODY><H1>501 Not Implemented</H1></BODY></HTML>",
                currentTime);
            break;

        default:
            return -1;
    }

    send(socket, str, strlen(str), 0);
    return 1;
}

// ---------------- MAIN ----------------
int main(int argc, char* argv[]) {
    if(argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    port_number = atoi(argv[1]);
    printf("Starting Proxy Server on port %d...\n", port_number);

    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if(proxy_socketId < 0) { perror("Socket failed"); exit(1); }

    int reuse = 1;
    setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        { perror("Bind failed"); exit(1); }

    if(listen(proxy_socketId, MAX_CLIENTS) < 0) { perror("Listen failed"); exit(1); }

    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);

    printf("Proxy running. Waiting for clients...\n");

    int clientSockets[MAX_CLIENTS];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int i = 0;

    while(1) {
        int clientSocket = accept(proxy_socketId, (struct sockaddr*)&client_addr, &client_len);
        if(clientSocket < 0) { perror("Accept failed"); continue; }

        clientSockets[i] = clientSocket;
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Client connected: %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        pthread_create(&tid[i], NULL, thread_fn, &clientSockets[i]);
        i++;
    }

    close(proxy_socketId);
    return 0;
}

// ---------------- CACHE FUNCTIONS ----------------
cache_element* find(char* url) {
    cache_element* site = NULL;
    pthread_mutex_lock(&lock);

    if(head != NULL) {
        site = head;
        while (site != NULL) {
            if(!strcmp(site->url, url)) {
                printf("Cache HIT: %s\n", url);   // <-- print cache hit
                site->lru_time_track = time(NULL); // update LRU
                pthread_mutex_unlock(&lock);
                return site;
            }
            site = site->next;
        }
    }

    printf("Cache MISS: %s\n", url);   // <-- print cache miss
    pthread_mutex_unlock(&lock);
    return NULL;
}

void remove_cache_element() {
    pthread_mutex_lock(&lock);
    if(!head) { pthread_mutex_unlock(&lock); return; }

    cache_element *p = head, *q = head, *temp = head;
    while(q->next) {
        if(q->next->lru_time_track < temp->lru_time_track) {
            temp = q->next;
            p = q;
        }
        q = q->next;
    }

    if(temp == head) head = head->next;
    else p->next = temp->next;

    cache_size -= temp->len + sizeof(cache_element) + strlen(temp->url) + 1;
    free(temp->data);
    free(temp->url);
    free(temp);

    pthread_mutex_unlock(&lock);
}

int add_cache_element(char* data,int size,char* url) {
    pthread_mutex_lock(&lock);
    int element_size = size + 1 + strlen(url) + sizeof(cache_element);
    while(cache_size + element_size > MAX_BYTES) remove_cache_element();

    cache_element* element = (cache_element*)malloc(sizeof(cache_element));
    element->data = strdup(data);
    element->url = strdup(url);
    element->lru_time_track = time(NULL);
    element->next = head;
    element->len = size;
    head = element;
    cache_size += element_size;

    pthread_mutex_unlock(&lock);
    return 1;
}
