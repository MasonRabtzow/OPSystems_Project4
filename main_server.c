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

#define PORT_NUM 9001

void error(const char *msg) {
    perror(msg);
    exit(1);
}

// Extended user structure
typedef struct _USR {
    int clisockfd;
    char ip[INET_ADDRSTRLEN];
    char name[50];
    char color[20];
    struct _USR* next;
} USR;

USR *head = NULL;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

// Safely add a client to the linked list
void add_client(int fd, const char* ip, const char* name, const char* color) {
    USR* new_usr = (USR*) malloc(sizeof(USR));
    new_usr->clisockfd = fd;
    strcpy(new_usr->ip, ip);
    strcpy(new_usr->name, name);
    strcpy(new_usr->color, color);
    
    pthread_mutex_lock(&list_mutex);
    new_usr->next = head;
    head = new_usr;
    pthread_mutex_unlock(&list_mutex);
}

// Safely remove a client
void remove_client(int fd) {
    pthread_mutex_lock(&list_mutex);
    USR* curr = head;
    USR* prev = NULL;
    while (curr != NULL) {
        if (curr->clisockfd == fd) {
            if (prev == NULL) head = curr->next;
            else prev->next = curr->next;
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&list_mutex);
}

// Print the up-to-date client list to the server console
void print_clients() {
    pthread_mutex_lock(&list_mutex);
    printf("\n--- Current Connected Clients ---\n");
    USR* curr = head;
    if (curr == NULL) {
        printf("No clients connected.\n");
    }
    while (curr != NULL) {
        printf("- %s (%s)\n", curr->name, curr->ip);
        curr = curr->next;
    }
    printf("---------------------------------\n\n");
    pthread_mutex_unlock(&list_mutex);
}
