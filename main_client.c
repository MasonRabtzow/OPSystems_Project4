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
