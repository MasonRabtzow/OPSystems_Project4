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
#include <signal.h>
#include <time.h>

#define PORT_NUM 9001

//color palette
const char* palette[15] = {
    "\033[38;5;196m", "\033[38;5;46m",  "\033[38;5;226m", "\033[38;5;33m",  
    "\033[38;5;201m", "\033[38;5;51m",  "\033[38;5;214m", "\033[38;5;99m",  
    "\033[38;5;211m", "\033[38;5;43m",  "\033[38;5;154m", "\033[38;5;220m", 
    "\033[38;5;203m", "\033[38;5;69m",  "\033[38;5;121m"  
};

//color indices to shuffle
int color_hat[15] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

//shuffle 
void shuffle_colors() {
    for (int i = 14; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = color_hat[i];
        color_hat[i] = color_hat[j];
        color_hat[j] = temp;
    }
}

//check for errors
void error(const char *msg) {
    perror(msg);
    exit(0);
}

//threads
typedef struct _ThreadArgs {
    int clisockfd;
} ThreadArgs;


void* thread_main_recv(void* args) {
    pthread_detach(pthread_self());
    int sockfd = ((ThreadArgs*) args)->clisockfd;
    free(args);

    char buffer[1024]; 
    int n;

    //state tracking
    char tracked_names[100][100];
    int tracked_colors[100];
    int tracked_count = 0;

    while (1) {
        memset(buffer, 0, 1024);
        n = recv(sockfd, buffer, 1024, 0);
        if (n <= 0) {
            printf("\nDisconnected from server.\n");
            exit(0);
        }

        //verify message format
        if (buffer[0] == '[') {
            char identifier[100];
            memset(identifier, 0, 100);
            
            //extract contents in brackets
            if (sscanf(buffer, "[%99[^]]", identifier) == 1) {
                
                int color_idx = 0;
                int found = 0;
                
                //look for color
                for (int i = 0; i < tracked_count; i++) {
                    if (strcmp(tracked_names[i], identifier) == 0) {
                        color_idx = tracked_colors[i];
                        found = 1;
                        break;
                    }
                }
                
                //new user gets new color
                if (!found && tracked_count < 100) {
                    strcpy(tracked_names[tracked_count], identifier);
                    
                    //get next color or loop if > 15 users
                    int hat_index = tracked_count % 15;
                    tracked_colors[tracked_count] = color_hat[hat_index]; 
                    
                    color_idx = tracked_colors[tracked_count];
                    tracked_count++;
                }

                //print message with color
                printf("\r%s%s\033[0m\n", palette[color_idx], buffer);
                continue;
            }
        }
        
        //join/leave message
        printf("\r\033[1;30m%s\033[0m\n", buffer); 
    }

    return NULL;
}

//thread for sending messages to server
void* thread_main_send(void* args) {
    int sockfd = ((ThreadArgs*) args)->clisockfd;
    free(args);

    char buffer[256];
    int n;

    while (1) {
        memset(buffer, 0, 256);
        
        if (fgets(buffer, 255, stdin) == NULL) {
            clearerr(stdin);
            continue; 
        }
        
        buffer[strcspn(buffer, "\n")] = 0;

        if (strlen(buffer) == 0) continue; 
        if (strcmp(buffer, "/quit") == 0) break;

        n = send(sockfd, buffer, strlen(buffer), 0);
        if (n < 0) error("ERROR writing to socket");
    }

    return NULL;
}

//main
int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    
    //shuffle color on startup
    srand(time(NULL));
    shuffle_colors();

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

    //send initial request
    char initial_request[64];
    if (argc >= 3) {
        strcpy(initial_request, argv[2]); 
    } else {
        strcpy(initial_request, "LIST"); 
    }
    send(sockfd, initial_request, strlen(initial_request), 0);
    
    char response[1024];
    memset(response, 0, 1024);
    int n = recv(sockfd, response, 1023, 0);
    if (n <= 0) error("ERROR reading response from server");
    response[n] = '\0'; 

    //handle server response
    if (strncmp(response, "AUTONEW ", 8) == 0) {
        int room_id = atoi(response + 8);
        printf("Connected to %s with new room number %d\n", inet_ntoa(serv_addr.sin_addr), room_id);
    } 
    else if (strncmp(response, "LIST\n", 5) == 0) {
        printf("\n%s\n", response + 5);
        char choice[64] = "";
        while (strlen(choice) == 0) {
            printf("Choose the room number or type [new] to create a new room: ");
            fgets(choice, 63, stdin);
            choice[strcspn(choice, "\n")] = 0;
        }
        send(sockfd, choice, strlen(choice), 0);

        memset(response, 0, 1024);
        n = recv(sockfd, response, 1023, 0);
        if (n <= 0) error("ERROR reading final response from server");
        response[n] = '\0';

        if (strncmp(response, "ERR", 3) == 0) {
            printf("Connection Rejected: Room does not exist.\n");
            close(sockfd);
            exit(1);
        } else if (strncmp(response, "OK ", 3) == 0) {
            int room_id = atoi(response + 3);
            printf("Connected to %s with room number %d\n", inet_ntoa(serv_addr.sin_addr), room_id);
        } else {
            printf("Unknown server response. Disconnecting.\n");
            close(sockfd);
            exit(1);
        }
    }
    else if (strncmp(response, "ERR", 3) == 0) {
        printf("Connection Rejected: Room '%s' does not exist.\n", initial_request);
        close(sockfd);
        exit(1);
    } else if (strncmp(response, "OK ", 3) == 0) {
        int room_id = atoi(response + 3);
        printf("Connected to %s with room number %d\n", inet_ntoa(serv_addr.sin_addr), room_id);
    } else {
        printf("Unknown server response. Disconnecting.\n");
        close(sockfd);
        exit(1);
    }

    //get user name 
    char name[50];
    memset(name, 0, 50); 
    
    while (strlen(name) == 0) {
        printf("\nEnter your name to join the chat: ");
        if (fgets(name, 49, stdin) != NULL) {
            name[strcspn(name, "\n")] = 0; 
        } else {
            clearerr(stdin); 
        }
    }

    send(sockfd, name, strlen(name), 0);
    
    printf("\nYou are in the room. Type /quit to exit.\n\n");

    //create threads
    pthread_t tid1, tid2;
    ThreadArgs* args;
    
    args = (ThreadArgs*) malloc(sizeof(ThreadArgs));
    args->clisockfd = sockfd;
    pthread_create(&tid1, NULL, thread_main_send, (void*) args);

    args = (ThreadArgs*) malloc(sizeof(ThreadArgs));
    args->clisockfd = sockfd;
    pthread_create(&tid2, NULL, thread_main_recv, (void*) args);

    pthread_join(tid1, NULL);

    close(sockfd);
    return 0;
}
