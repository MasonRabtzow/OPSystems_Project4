#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <signal.h> // Added for SIGPIPE handling

#define PORT_NUM 9001

void error(const char *msg) {
    perror(msg);
    exit(1);
}

// User structure
typedef struct _USR {
    int clisockfd;
    char ip[INET_ADDRSTRLEN];
    char name[50];
    char color[20];
    struct _USR* next;
} USR;
