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
