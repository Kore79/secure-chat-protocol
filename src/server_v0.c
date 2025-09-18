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

// global variables (for simplicity in this version)
int client_sockets[MAX_CLIENTS] = {0}; // array to track connected clients socket

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
    struct sockaddr_in address;
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
            sd = client_sockets[i]; // get socket descriptor from array

            // if socket is valid, add it to the set we want to monitor
            if(sd > 0) {
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
                if (client_sockets[i] == 0) { // find an empty slot
                    client_sockets[i] = new_socket;
                    printf("Adding to list of sockets as index %d\n", i);
                    break;
                }
            }
        }

        // Then, check for activity on client sockets
        // This loops handles all client communication: recieving data and broadcasting
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = client_sockets[i]; // get socket descriptor for this client

            // check if this client socket is in the set of descriptors with activity
            if (FD_ISSET(sd, &readfds)) {
                // try to recieve data from client
                if ((valread = recv(sd, buffer, BUFFER_SIZE, 0)) == 0) {
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("Host disconnected, IP: %s,  PORT: %d, Socket FD: %d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port), sd);
                    close(sd);
                    client_sockets[i] = 0;
                } else {
                    // recv() returned data, we recieved a message

                    buffer[valread] = '\0';

                    printf("Recieved from client (FD: %d): %s", sd, buffer);

                    // TODO: add code to broadcat msg to all other clients.
                }
            }
        }
    } // end while(1_

    close(server_fd);
    return 0;
}
