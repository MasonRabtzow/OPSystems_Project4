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

// Broadcast a message to everyone except the sender
void broadcast(int fromfd, const char* message) {
    pthread_mutex_lock(&list_mutex);
    USR* cur = head;
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
    pthread_mutex_unlock(&list_mutex);
}

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

    char name[50];
    int nrcv;

    // First message from client should be their name
    nrcv = recv(clisockfd, name, 49, 0);
    if (nrcv <= 0) {
        close(clisockfd);
        return NULL;
    }
    name[nrcv] = '\0';
    name[strcspn(name, "\n")] = 0; // Strip newline

    // Assign a random ANSI color code (31 to 36 are text colors)
    char color[20];
    snprintf(color, sizeof(color), "\033[1;3%dm", (rand() % 6) + 1);

    // Add to list and update server console
    add_client(clisockfd, ip, name, color);
    print_clients();

    // Broadcast Join Message
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s%s (%s) has joined the room\033[0m", color, name, ip);
    broadcast(clisockfd, buffer);

    // Main communication loop
    while (1) {
        memset(buffer, 0, 512);
        nrcv = recv(clisockfd, buffer, 255, 0);
        
        if (nrcv <= 0) break; // Client disconnected or error

        buffer[nrcv] = '\0';
        buffer[strcspn(buffer, "\n")] = 0; // Strip newline
        
        if (strlen(buffer) == 0) continue;

        // Format and broadcast: [Name (IP)]: Message
        char formatted_msg[1024]; 
        snprintf(formatted_msg, sizeof(formatted_msg), "%s[%s (%s)]: %s\033[0m", color, name, ip, buffer);
        broadcast(clisockfd, formatted_msg);
    }

    // Handle Disconnect
    remove_client(clisockfd);
    print_clients();
    
    // Broadcast Leave Message (Using grey/default color for system messages)
    snprintf(buffer, sizeof(buffer), "\033[1;30m%s (%s) has left the room\033[0m", name, ip);
    broadcast(clisockfd, buffer);

    close(clisockfd);
    return NULL;
}


int main(int argc, char *argv[]) {
    srand(time(NULL)); // Seed random number generator for colors

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    // Allow quick port reuse
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
