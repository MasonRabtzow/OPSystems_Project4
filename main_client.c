#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> 
#include <pthread.h>

#define PORT_NUM 9001

void error(const char *msg) {
    perror(msg);
    exit(0);
}

typedef struct _ThreadArgs {
    int clisockfd;
} ThreadArgs;

void* thread_main_recv(void* args) {
    pthread_detach(pthread_self());
    int sockfd = ((ThreadArgs*) args)->clisockfd;
    free(args);

    char buffer[1024]; 
    int n;

    while (1) {
        memset(buffer, 0, 1024);
        n = recv(sockfd, buffer, 1024, 0);
        if (n <= 0) {
            printf("\nDisconnected from server.\n");
            exit(0);
        }
        printf("\r%s\n", buffer); 
    }

    return NULL;
}

void* thread_main_send(void* args) {
    pthread_detach(pthread_self());
    int sockfd = ((ThreadArgs*) args)->clisockfd;
    free(args);

    char buffer[256];
    int n;

    while (1) {
        memset(buffer, 0, 256);
        
        if (fgets(buffer, 255, stdin) == NULL) break;
        buffer[strcspn(buffer, "\n")] = 0;

        if (strlen(buffer) == 0) continue; 
        if (strcmp(buffer, "/quit") == 0) break;

        n = send(sockfd, buffer, strlen(buffer), 0);
        if (n < 0) error("ERROR writing to socket");
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    // Room argument is now optional
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <hostname> [new | room_number]\n", argv[0]);
        exit(1);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    struct sockaddr_in serv_addr;
    memset((char*) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(PORT_NUM);

    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        struct hostent* server = gethostbyname(argv[1]);
        if (server == NULL) error("ERROR, no such host");
        memcpy((char*)server->h_addr, (char*)&serv_addr.sin_addr.s_addr, server->h_length);
    }

    printf("Connecting to server...\n");
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
        error("ERROR connecting");
