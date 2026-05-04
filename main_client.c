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

// Room structure containing its own client list and mutex
typedef struct _ROOM {
    int room_id;
    USR* clients_head;
    pthread_mutex_t room_mutex;
    struct _ROOM* next;
} ROOM;

ROOM *room_head = NULL;
pthread_mutex_t global_room_mutex = PTHREAD_MUTEX_INITIALIZER;
int next_room_id = 1;

// Safely add a client to a specific room
void add_client_to_room(ROOM* room, int fd, const char* ip, const char* name, const char* color) {
    USR* new_usr = (USR*) malloc(sizeof(USR));
    new_usr->clisockfd = fd;
    strcpy(new_usr->ip, ip);
    strcpy(new_usr->name, name);
    strcpy(new_usr->color, color);
    
    pthread_mutex_lock(&room->room_mutex);
    new_usr->next = room->clients_head;
    room->clients_head = new_usr;
    pthread_mutex_unlock(&room->room_mutex);
}

// Safely remove a client from a specific room
void remove_client_from_room(ROOM* room, int fd) {
    pthread_mutex_lock(&room->room_mutex);
    USR* curr = room->clients_head;
    USR* prev = NULL;
    while (curr != NULL) {
        if (curr->clisockfd == fd) {
            if (prev == NULL) room->clients_head = curr->next;
            else prev->next = curr->next;
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&room->room_mutex);
}

// Safely delete a room if there are no clients left in it
void check_and_remove_empty_room(ROOM* target_room) {
    pthread_mutex_lock(&global_room_mutex);
    pthread_mutex_lock(&target_room->room_mutex);
    int is_empty = (target_room->clients_head == NULL);
    pthread_mutex_unlock(&target_room->room_mutex);

    if (is_empty) {
        ROOM* curr = room_head;
        ROOM* prev = NULL;
        while (curr != NULL) {
            if (curr == target_room) {
                if (prev == NULL) room_head = curr->next;
                else prev->next = curr->next;

                pthread_mutex_destroy(&curr->room_mutex);
                free(curr);
                printf("=> Room %d became empty and was deleted.\n", target_room->room_id);
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    pthread_mutex_unlock(&global_room_mutex);
}

// Print the up-to-date client list organized by rooms
void print_clients() {
    pthread_mutex_lock(&global_room_mutex);
    printf("\n--- Current Connected Clients By Room ---\n");
    ROOM* r = room_head;
    if (r == NULL) {
        printf("No active rooms.\n");
    }
    while (r != NULL) {
        pthread_mutex_lock(&r->room_mutex);
        printf("[Room %d]\n", r->room_id);
        USR* c = r->clients_head;
        if (c == NULL) printf("  (Empty)\n");
        while (c != NULL) {
            printf("  - %s (%s)\n", c->name, c->ip);
            c = c->next;
        }
        pthread_mutex_unlock(&r->room_mutex);
        r = r->next;
    }
    printf("-----------------------------------------\n\n");
    pthread_mutex_unlock(&global_room_mutex);
}

// Broadcast a message to everyone in a specific room
void broadcast_to_room(ROOM* room, int fromfd, const char* message) {
    pthread_mutex_lock(&room->room_mutex);
    USR* cur = room->clients_head;
    int nmsg = strlen(message);
    
    while (cur != NULL) {
        if (cur->clisockfd != fromfd) {
            int nsen = send(cur->clisockfd, message, nmsg, 0);
            if (nsen != nmsg) {
                perror("ERROR send() failed during broadcast");
            }
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&room->room_mutex);
}

typedef struct _ThreadArgs {
    int clisockfd;
    char ip[INET_ADDRSTRLEN];
} ThreadArgs;

void* thread_main(void* args) {
    pthread_detach(pthread_self());
