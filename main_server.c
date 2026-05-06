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

// --- Helper: Find the lowest available room ID ---
// NOTE: This must be called while global_room_mutex is locked!
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
        // If no active room holds this ID, it's free to use
        if (!exists) {
            return candidate_id;
        }
        candidate_id++;
    }
}


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

                // FIX: Store the ID before we destroy the memory!
                int deleted_id = curr->room_id; 

                pthread_mutex_destroy(&curr->room_mutex);
                free(curr); // Memory is handed back to OS here
                
                // Now we safely print the stored integer
                printf("=> Room %d became empty and was deleted.\n", deleted_id);
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    pthread_mutex_unlock(&global_room_mutex);
}

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

void* thread_main(void* args) {
    pthread_detach(pthread_self());

    int clisockfd = ((ThreadArgs*) args)->clisockfd;
    char ip[INET_ADDRSTRLEN];
    strcpy(ip, ((ThreadArgs*) args)->ip);
    free(args);

    char buffer[1024];
    int nrcv;

    // 1. Initial Handshake: Wait for room request
    nrcv = recv(clisockfd, buffer, 255, 0);
    if (nrcv <= 0) { close(clisockfd); return NULL; }
    buffer[nrcv] = '\0';
    buffer[strcspn(buffer, "\n")] = 0;

    ROOM* my_room = NULL;
    char response[1024];

    // Check if the client sent an invalid room number or explicitly asked for a list
    if (strcmp(buffer, "new") != 0 && atoi(buffer) <= 0) {
        pthread_mutex_lock(&global_room_mutex);
        
        if (room_head == NULL) {
            // AUTONEW: No rooms exist, force create one
            my_room = (ROOM*) malloc(sizeof(ROOM));
            my_room->room_id = next_room_id++;
            my_room->clients_head = NULL;
            pthread_mutex_init(&my_room->room_mutex, NULL);
            
            my_room->next = room_head;
            room_head = my_room;
            pthread_mutex_unlock(&global_room_mutex);

            snprintf(response, sizeof(response), "AUTONEW %d", my_room->room_id);
            send(clisockfd, response, strlen(response), 0);
        } else {
            // LIST: Rooms exist, compile the interactive menu
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

            // Send list menu to client
            send(clisockfd, response, strlen(response), 0);

            // Wait for client to make a choice
            nrcv = recv(clisockfd, buffer, 255, 0);
            if (nrcv <= 0) { close(clisockfd); return NULL; }
            buffer[nrcv] = '\0';
            buffer[strcspn(buffer, "\n")] = 0;
        }
    }
    // 2. Process room assignment (unless AUTONEW already handled it)
        if (my_room == NULL) {
            if (strcmp(buffer, "new") == 0) {
                // Create a new room
                pthread_mutex_lock(&global_room_mutex);
                my_room = (ROOM*) malloc(sizeof(ROOM));
                my_room->room_id = next_room_id++;
                my_room->clients_head = NULL;
                pthread_mutex_init(&my_room->room_mutex, NULL);
                
                my_room->next = room_head;
                room_head = my_room;
                pthread_mutex_unlock(&global_room_mutex);

                snprintf(response, sizeof(response), "OK %d", my_room->room_id);
                send(clisockfd, response, strlen(response), 0);
            } else {
                // Search for existing room
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
                    // Room not found, reject client
                    snprintf(response, sizeof(response), "ERR");
                    send(clisockfd, response, strlen(response), 0);
                    close(clisockfd);
                    return NULL;
                }
            }
        }
        // 3. Receive the client's name
        char name[50];
        nrcv = recv(clisockfd, name, 49, 0);
        if (nrcv <= 0) { close(clisockfd); return NULL; }
        name[nrcv] = '\0';
        name[strcspn(name, "\n")] = 0; 

        char color[20];
        snprintf(color, sizeof(color), "\033[1;3%dm", (rand() % 6) + 1);

        add_client_to_room(my_room, clisockfd, ip, name, color);
        print_clients();

        snprintf(buffer, sizeof(buffer), "%s%s (%s) joined the chat room! %d\033[0m", color, name, ip, my_room->room_id);
        broadcast_to_room(my_room, clisockfd, buffer);

        // 4. Main communication loop
        while (1) {
            memset(buffer, 0, 1024);
            nrcv = recv(clisockfd, buffer, 511, 0); 
            
            if (nrcv <= 0) break; 

            buffer[nrcv] = '\0';
            buffer[strcspn(buffer, "\n")] = 0; 
            
            if (strlen(buffer) == 0) continue;

            char formatted_msg[1024]; 
            snprintf(formatted_msg, sizeof(formatted_msg), "%s[%s (%s)]: %s\033[0m", color, name, ip, buffer);
            broadcast_to_room(my_room, clisockfd, formatted_msg);
        }

        // 5. Handle Disconnect
        remove_client_from_room(my_room, clisockfd);
        
        snprintf(buffer, sizeof(buffer), "\033[1;30m%s (%s) has left Room %d\033[0m", name, ip, my_room->room_id);
        broadcast_to_room(my_room, clisockfd, buffer);

        check_and_remove_empty_room(my_room);
        print_clients();

        close(clisockfd);
        return NULL;
}

int main(int argc, char *argv[]) {
    // Ignore broken pipe signals to prevent server crashes
    signal(SIGPIPE, SIG_IGN); 

    srand(time(NULL)); 

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