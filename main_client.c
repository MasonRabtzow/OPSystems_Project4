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
