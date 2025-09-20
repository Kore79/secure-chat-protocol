// Version 0.1 - The Hackable Server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

typedef struct {
    int socket_fd; // client's socket file descriptor
    char name[32]; // username client has chosen
} client_info_t;


// global array to track connected clients
client_info_t clients[MAX_CLIENTS] = {0}; // Initialize all to zero


// Function to broadcast a message to all connected clients except the sender
void broadcast_message(int sender_socket, const char *username, const char *message) {
    char formatted_message[BUFFER_SIZE];

    // Format the message as "[username] message"
    snprintf(formatted_message, BUFFER_SIZE, "[%s] %s", username, message);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        int dest_socket = clients[i].socket_fd;

        // check if the socket is valid and it's not the sender
        if (dest_socket > 0 && dest_socket != sender_socket) {
            send(dest_socket, formatted_message, strlen(formatted_message), 0);
        }
    }

    // print message on the server console
    printf("%s", formatted_message);
}


// Helper function to find a client's index in the array by their socket fd
int find_client_index(int socket_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd == socket_fd) {
            return i; // found client
        }
    }

    return -1;
}


// Function to initialize the server socket
int server_socket_setup()
{
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    // Create a socket (IPv4, TCP)
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // forcefully attach socket to the port , even if it's in use
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // define server address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // bind to all available interfaces
    address.sin_port = htons(PORT);

    // Bind socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed.");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // start listening for incoming connections
    // The second argument (3) is the "backlog" - the queue for pending connections
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d. Waiting for connections...\n", PORT);
    return server_fd;
}


int main(void) {
    int server_fd, new_socket, fdmax, activity, i, valread, sd;
    struct sockaddr_in address; // client address
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    fd_set readfds; // set of file descriptors we want to monitor for reading

    // Set up server socket
    server_fd = server_socket_setup();

    // main server loop
    while(1) {
        // clear the socket set
        FD_ZERO(&readfds);

        // add listening socket to the set
        FD_SET(server_fd, &readfds);
        fdmax = server_fd; // keep track of the highest numbered file descriptor

        // add all valid clients socket to the set
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i].socket_fd; // get socket descriptor from array

            // if socket is valid, add it to the set we want to monitor
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }

            // keep track of max file descriptor number
            if (sd > fdmax) {
                fdmax = sd;
            }

        }

        // Wait indefinitely for activity on one of the sockets.
        // The 'readfds' set will be modified to show sockets are ready
        activity = select(fdmax + 1, &readfds, NULL, NULL, NULL);

        if (activity < 0) {
            perror("select error");
        }

        if (FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept error");
                exit(EXIT_FAILURE);
            }

            // print the IP address of the new client
            printf("New connection: socket fd is %d, IP is %s, PORT; %d\n", new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            // add new socket to our array of client sockets
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].socket_fd == 0) { // find an empty slot
                    clients[i].socket_fd = new_socket;
                    clients[i].name[0] = '\0'; // initialize username to empty

                    printf("Adding new client to slot %d, socket fd: %d\n", i, new_socket);
                    // Send a welcome message prompting for a name
                    send(new_socket, "Welcome! Please set your name with: NAME <yourname>\n", 52, 0);
                    break;
                }
            }
        }

        // Then, check for activity on client sockets
        // This loops handles all client communication: recieving data and broadcasting
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i].socket_fd; // get socket descriptor for this client

            // check if this client socket is in the set of descriptors with activity
            if (FD_ISSET(sd, &readfds)) {
                // try to recieve data from client
                if ((valread = recv(sd, buffer, BUFFER_SIZE, 0)) == 0) {
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("Host disconnected, IP: %s,  PORT: %d, Socket FD: %d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port), sd);
                    close(sd);
                    printf("Cleared client from slot %d (Name: %s)\n", i, clients[i].name);
                    clients[i].socket_fd = 0; // clear socket
                    clients[i].name[0] = '\0'; // clear username
                } else {
                    // recv() returned data, we recieved a message

                    buffer[valread] = '\0'; // Null- terminate the string
                    int client_index = find_client_index(sd);
                    if (client_index == -1) {
                        printf("Error: Recieved data from an unknown socket %d\n", sd);
                        close(sd);
                        continue;
                    }

                    // parse the command (simplistic parsing by looking for a space or new line
                    char *command = strtok(buffer, " \n"); // get the first word (NAME or SAY)
                    char *argument = strtok(NULL, "\n"); // get the rest of the line

                    if (command == NULL) {
                        // No command recieved, ignore
                        continue;
                    }

                    if (strcmp(command, "NAME") == 0) {
                        // NAME command: set the client's username
                        if (argument == NULL) {
                            send(sd, "ERROR: NAME requires a username\n", 32, 0);
                        } else {
                            strncpy(clients[client_index].name, argument, 31);
                            clients[client_index].name[31] = '\0';
                            printf("Client on socket %d is now known as: %s\n", sd, argument);
                            send(sd, "OK\n", 3, 0);
                        }
                    } else if (strcmp(command, "SAY") == 0) {
                        // SAY command: Broadcast the message to everyone else
                        if (argument == NULL) {
                            send(sd, "ERROR: SAY requires a message\n", 30, 0);
                        } else {
                            // check if client has set a name
                            if (strlen(clients[client_index].name) == 0) {
                                send(sd, "ERROR: Set your name with NAME <username> first\n", 48, 0);
                            } else {
                                broadcast_message(sd, clients[client_index].name, argument);
                            }
                        }
                    } else {
                        // unkown command
                        send(sd, "ERROR: Unknown command. Use 'NAME <username>' or 'SAY <message>'\n", 64, 0);
                    }
                }
            }
        }
    }

    return 0;
}
