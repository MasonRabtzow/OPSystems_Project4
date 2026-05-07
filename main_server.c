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
#include <signal.h> 

#define PORT_NUM 9001

void error(const char *msg) {
    perror(msg);
    exit(1);
}

typedef struct _USR {
    int clisockfd;
    char ip[INET_ADDRSTRLEN];
    char name[50];
    struct _USR* next;
} USR;

typedef struct _ROOM {
    int room_id;
    USR* clients_head;
    pthread_mutex_t room_mutex;
    struct _ROOM* next;
} ROOM;

ROOM *room_head = NULL;
pthread_mutex_t global_room_mutex = PTHREAD_MUTEX_INITIALIZER;

//find lowest available room ID
int get_next_available_room_id() {
    int candidate_id = 1;
    while (1) {
        int exists = 0;
        ROOM* r = room_head;
        while (r != NULL) {
            if (r->room_id == candidate_id) {
                exists = 1;
                break;
            }
            r = r->next;
        }
        //no active room holds an ID then use
        if (!exists) {
            return candidate_id;
        }
        candidate_id++;
    }
}

//add client
void add_client_to_room(ROOM* room, int fd, const char* ip, const char* name) {
    USR* new_usr = (USR*) malloc(sizeof(USR));
    new_usr->clisockfd = fd;
    strcpy(new_usr->ip, ip);
    strcpy(new_usr->name, name);
    
    pthread_mutex_lock(&room->room_mutex);
    new_usr->next = room->clients_head;
    room->clients_head = new_usr;
    pthread_mutex_unlock(&room->room_mutex);
}

//remove client
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

//check/remove empty room
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

                //store ID before mem is destroyed
                int deleted_id = curr->room_id; 

                pthread_mutex_destroy(&curr->room_mutex);
                free(curr); //give mem back to os
                
                //print what is stored
                printf("=> Room %d became empty and was deleted.\n", deleted_id);
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    pthread_mutex_unlock(&global_room_mutex);
}

//debug print
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
//broadcast message to all clients in the room except sender
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
//threads for each connection
typedef struct _ThreadArgs {
    int clisockfd;
    char ip[INET_ADDRSTRLEN];
} ThreadArgs;

void* thread_main(void* args) {
    pthread_detach(pthread_self());

    int clisockfd = ((ThreadArgs*) args)->clisockfd;
    char ip[INET_ADDRSTRLEN];
    strcpy(ip, ((ThreadArgs*) args)->ip);
    free(args);

    char buffer[1024];
    int nrcv;

    //room selection/creation
    nrcv = recv(clisockfd, buffer, 255, 0);
    if (nrcv <= 0) { close(clisockfd); return NULL; }
    buffer[nrcv] = '\0';
    buffer[strcspn(buffer, "\n")] = 0;

    ROOM* my_room = NULL;
    char response[1024];

    if (strcmp(buffer, "new") != 0 && atoi(buffer) <= 0) {
        pthread_mutex_lock(&global_room_mutex);
        
        if (room_head == NULL) {
            my_room = (ROOM*) malloc(sizeof(ROOM));
            my_room->room_id = get_next_available_room_id(); //pick lowest ID
            my_room->clients_head = NULL;
            pthread_mutex_init(&my_room->room_mutex, NULL);
            
            my_room->next = room_head;
            room_head = my_room;
            pthread_mutex_unlock(&global_room_mutex);

            snprintf(response, sizeof(response), "AUTONEW %d", my_room->room_id);
            send(clisockfd, response, strlen(response), 0);
        } else {
            strcpy(response, "LIST\n--- Available Rooms ---\n");
            ROOM* curr = room_head;
            while (curr != NULL) {
                pthread_mutex_lock(&curr->room_mutex);
                int count = 0;
                USR* c = curr->clients_head;
                while (c) { count++; c = c->next; }
                pthread_mutex_unlock(&curr->room_mutex);

                char line[64];
                snprintf(line, sizeof(line), "  Room %d: %d person(s)\n", curr->room_id, count);
                strcat(response, line);
                curr = curr->next;
            }
            pthread_mutex_unlock(&global_room_mutex);

            send(clisockfd, response, strlen(response), 0);

            nrcv = recv(clisockfd, buffer, 255, 0);
            if (nrcv <= 0) { close(clisockfd); return NULL; }
            buffer[nrcv] = '\0';
            buffer[strcspn(buffer, "\n")] = 0;
        }
    }

    if (my_room == NULL) {
        if (strcmp(buffer, "new") == 0) {
            pthread_mutex_lock(&global_room_mutex);
            my_room = (ROOM*) malloc(sizeof(ROOM));
            my_room->room_id = get_next_available_room_id(); //lowest ID
            my_room->clients_head = NULL;
            pthread_mutex_init(&my_room->room_mutex, NULL);
            
            my_room->next = room_head;
            room_head = my_room;
            pthread_mutex_unlock(&global_room_mutex);

            snprintf(response, sizeof(response), "OK %d", my_room->room_id);
            send(clisockfd, response, strlen(response), 0);
        } else {
            int requested_id = atoi(buffer);
            pthread_mutex_lock(&global_room_mutex);
            ROOM* curr = room_head;
            while (curr != NULL) {
                if (curr->room_id == requested_id) {
                    my_room = curr;
                    break;
                }
                curr = curr->next;
            }
            pthread_mutex_unlock(&global_room_mutex);

            if (my_room != NULL) {
                snprintf(response, sizeof(response), "OK %d", my_room->room_id);
                send(clisockfd, response, strlen(response), 0);
            } else {
                snprintf(response, sizeof(response), "ERR");
                send(clisockfd, response, strlen(response), 0);
                close(clisockfd);
                return NULL;
            }
        }
    }

    //client name reg
    char name[50];
    nrcv = recv(clisockfd, name, 49, 0);
    if (nrcv <= 0) { close(clisockfd); return NULL; }
    name[nrcv] = '\0';
    name[strcspn(name, "\n")] = 0; 

    add_client_to_room(my_room, clisockfd, ip, name);
    print_clients();

    snprintf(buffer, sizeof(buffer), "*** %s (%s) has joined Room %d", name, ip, my_room->room_id);
    broadcast_to_room(my_room, clisockfd, buffer);

    //message handling loop
    while (1) {
        memset(buffer, 0, 1024);
        nrcv = recv(clisockfd, buffer, 511, 0); 
        
        if (nrcv <= 0) break; 

        buffer[nrcv] = '\0';
        buffer[strcspn(buffer, "\n")] = 0; 
        
        if (strlen(buffer) == 0) continue;

        char formatted_msg[2048]; 
        snprintf(formatted_msg, sizeof(formatted_msg), "[%s (%s)]: %s", name, ip, buffer);
        broadcast_to_room(my_room, clisockfd, formatted_msg);
    }

    //disconnect cleanup
    remove_client_from_room(my_room, clisockfd);
    
    snprintf(buffer, sizeof(buffer), "*** %s (%s) has left Room %d", name, ip, my_room->room_id);
    broadcast_to_room(my_room, clisockfd, buffer);

    check_and_remove_empty_room(my_room);
    print_clients();

    close(clisockfd);
    return NULL;
}

//main
int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN); 

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    socklen_t slen = sizeof(serv_addr);
    memset((char*) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;	
    serv_addr.sin_port = htons(PORT_NUM);

    if (bind(sockfd, (struct sockaddr*) &serv_addr, slen) < 0) 
        error("ERROR on binding");

    listen(sockfd, 10);
    printf("Server started on port %d. Waiting for connections...\n", PORT_NUM);

    while(1) {
        struct sockaddr_in cli_addr;
        socklen_t clen = sizeof(cli_addr);
        int newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clen);
        if (newsockfd < 0) error("ERROR on accept");

        ThreadArgs* args = (ThreadArgs*) malloc(sizeof(ThreadArgs));
        args->clisockfd = newsockfd;
        strcpy(args->ip, inet_ntoa(cli_addr.sin_addr));

        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_main, (void*) args) != 0) 
            error("ERROR creating a new thread");
    }

    close(sockfd);
    return 0; 
}
