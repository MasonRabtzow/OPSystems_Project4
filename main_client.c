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

// Extended 256-color ANSI Palette
const char* palette[15] = {
    "\033[38;5;196m", "\033[38;5;46m",  "\033[38;5;226m", "\033[38;5;33m",  
    "\033[38;5;201m", "\033[38;5;51m",  "\033[38;5;214m", "\033[38;5;99m",  
    "\033[38;5;211m", "\033[38;5;43m",  "\033[38;5;154m", "\033[38;5;220m", 
    "\033[38;5;203m", "\033[38;5;69m",  "\033[38;5;121m"  
};

// --- Client-Side "Hat" System ---
int color_hat[15] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

// Shake the hat
void shuffle_colors() {
    for (int i = 14; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = color_hat[i];
        color_hat[i] = color_hat[j];
        color_hat[j] = temp;
    }
}

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

    // --- Localized State Tracking ---
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

        // Check if message is a standard chat: "[Name (IP)]: Message"
        if (buffer[0] == '[') {
            char identifier[100];
            memset(identifier, 0, 100);
            
            // Extract everything between the opening '[' and closing ']'
            if (sscanf(buffer, "[%99[^]]", identifier) == 1) {
                
                int color_idx = 0;
                int found = 0;
                
                // Search local dictionary for existing color
                for (int i = 0; i < tracked_count; i++) {
                    if (strcmp(tracked_names[i], identifier) == 0) {
                        color_idx = tracked_colors[i];
                        found = 1;
                        break;
                    }
                }
                
                // If it's a new user, draw from the local shuffled hat
                if (!found && tracked_count < 100) {
                    strcpy(tracked_names[tracked_count], identifier);
                    
                    // Pull the next random color from the hat (recycle if > 15 people join)
                    int hat_index = tracked_count % 15;
                    tracked_colors[tracked_count] = color_hat[hat_index]; 
                    
                    color_idx = tracked_colors[tracked_count];
                    tracked_count++;
                }

                // Print the fully colored message to the user's terminal
                printf("\r%s%s\033[0m\n", palette[color_idx], buffer);
                continue;
            }
        }
        
        // System join/leave message (starts with ***), print in standard gray
        printf("\r\033[1;30m%s\033[0m\n", buffer); 
    }

    return NULL;
}


int main(int argc, char *argv[]) {
    // Room argument is now optional
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

    // 1. Handshake Phase: Send initial request
    char initial_request[64];
    if (argc >= 3) {
        strcpy(initial_request, argv[2]); // Explicit argument passed
    } else {
        strcpy(initial_request, "LIST");  // Trigger the menu fallback
    }
    send(sockfd, initial_request, strlen(initial_request), 0);
    
    char response[1024];
    memset(response, 0, 1024);
    int n = recv(sockfd, response, 1023, 0);
    if (n <= 0) error("ERROR reading response from server");
    response[n] = '\0'; // Guarantee null termination

    // 2. Process Multi-Stage Server Response
    if (strncmp(response, "AUTONEW ", 8) == 0) {
        // Condition A: Zero rooms existed, server forced a new one
        int room_id = atoi(response + 8);
        printf("Connected to %s with new room number %d\n", inet_ntoa(serv_addr.sin_addr), room_id);
    } 
    else if (strncmp(response, "LIST\n", 5) == 0) {
        // Condition B: Rooms exist, display menu and prompt user
        printf("\n%s\n", response + 5);
        
        char choice[64] = "";
        while (strlen(choice) == 0) {
            printf("Choose the room number or type [new] to create a new room: ");
            fgets(choice, 63, stdin);
            choice[strcspn(choice, "\n")] = 0;
        }

        // Send choice back to server
        send(sockfd, choice, strlen(choice), 0);

        // Await the final OK or ERR
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
        // Condition C: Client passed a bad argument initially
        printf("Connection Rejected: Room '%s' does not exist.\n", initial_request);
        close(sockfd);
        exit(1);
    } else if (strncmp(response, "OK ", 3) == 0) {
        // Condition D: Client passed a valid room number or "new" initially
        int room_id = atoi(response + 3);
        printf("Connected to %s with room number %d\n", inet_ntoa(serv_addr.sin_addr), room_id);
    } else {
        printf("Unknown server response. Disconnecting.\n");
        close(sockfd);
        exit(1);
    }

    // 3. Collect and Send Name
    char name[50];
    printf("Enter your name to join the chat: ");
    fgets(name, 49, stdin);
    name[strcspn(name, "\n")] = 0; 
    send(sockfd, name, strlen(name), 0);
    
    printf("You are in the room. Type /quit to exit.\n\n");

    // 4. Create Threads
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
