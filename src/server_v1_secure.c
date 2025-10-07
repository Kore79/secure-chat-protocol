#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <errno.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define MAX_MESSAGE_SIZE 4096
#define MAX_NAME_SIZE 31
#define BUFFER_SIZE 1024

// TLV Protocol Constants
typedef enum {
    MSG_SET_NAME = 0x01,
    MSG_SEND_MESSAGE = 0x02,
    MSG_ERROR = 0x03,
    MSG_OK = 0x04
} message_type_t;

// compiler-specific packing to ensure 5-byte struct
#ifdef __GNUC__
#define PACKED __attribute__((packed))
#else
#define PACKED
#pragma pack(push, 1)
#endif

typedef struct PACKED {
    uint8_t type; // message type
    uint32_t length; // length of data (network byte order)
} message_header_t;

#ifndef __GNUC__
#pragma pack(pop)
#endif


// Client structure
typedef struct {
    int socket_fd;
    char name[MAX_NAME_SIZE];
    // Buffer for assembling partial messages
    uint8_t buffer[MAX_MESSAGE_SIZE];
    size_t buffer_len;
} client_info_t;

client_info_t clients[MAX_CLIENTS] = {0};

// function prototypes
int set_up_server_socket();
int send_message(int socket, message_type_t type, const char *data, uint32_t data_len);
void broadcast_message(int sender_socket, const char *username, const char *message);
int find_client_index(int socket_fd);
void handle_client_message(int client_socket, message_type_t type, const char *data, uint32_t data_len);
int process_client_data(client_info_t * client);


// Function to initialize the server socket
int set_up_server_socket()
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



int send_message(int socket, message_type_t type, const char *data, uint32_t data_len)
{
    uint8_t header[5];
    header[0] = type;
    header[1] = (data_len >> 24) & 0xFF; // highest byte first (network order)
    header[2] = (data_len >> 16) & 0xFF;
    header[3] = (data_len >> 8) & 0xFF;
    header[4] = data_len & 0xFF;

    // Send header
    if (send(socket, header, sizeof(header), 0) != sizeof(header)) {
        return -1;
    }

    // send data if any
    if (data_len > 0 && send(socket, data, data_len, 0) != data_len) {
        return -1;
    }

    return 0;
}


int process_client_data(client_info_t *client)
{
    ssize_t bytes_read = recv(client->socket_fd, client->buffer + client->buffer_len, MAX_MESSAGE_SIZE - client->buffer_len, 0);

    if (bytes_read <= 0)
        return  -1; // client disconnected or error

    client->buffer_len += bytes_read;

    // process complete message from buffer
    while (client->buffer_len >= 5) {
        // Extract header fields manually  to avoid struct alignment issues
        uint8_t type = client->buffer[0];
        uint32_t length = (client->buffer[1] << 24) | (client->buffer[2] << 16) | (client->buffer[3] << 8) | client->buffer[4];

        // reject oversize messages immediately
        if (length > MAX_MESSAGE_SIZE - 5) {
            return -1; // Disconnect malicious client
        }

        // check for complete message
        if (client->buffer_len >= 5 + length) {
            // process complete message
            char *message_data = (char *)(client->buffer + 5);
            if (length > 0) {
                message_data[length] = '\0';
            }

            handle_client_message(client->socket_fd, type, message_data, length);

            // Remove processed message from buffer
            size_t message_total_len = 5 + length;
            memmove(client->buffer, client->buffer + message_total_len, client->buffer_len - message_total_len);
            client->buffer_len -= message_total_len;
        } else {
            // Don't have full message yet, wait for more data
            printf("DEBUG: Incomplete message, waiting for more data\n");
            break;
        }
    }

    return 0;
}


void handle_client_message(int client_socket, message_type_t type, const char *data, uint32_t data_len)
{
    if (data_len > MAX_MESSAGE_SIZE) {
        printf("ERROR: data_len %u exceeds maximum\n", data_len);
        return;
    }

    // Only print data if it's a reasonable before accesing data
    if (data != NULL && data_len > 0 && data_len <= 100) {
        printf("DEBUG: data='%.*s'\n", data_len, data);
    }


    int client_index = find_client_index(client_socket);
    if (client_index == -1) {
        printf("Error: Message from unknown client socket %d\n", client_socket);
        return;
    }

    switch (type) {
        case MSG_SET_NAME:
            if (data_len > 0 && data_len <= MAX_NAME_SIZE) {
                strncpy(clients[client_index].name, data, data_len);
                clients[client_index].name[data_len] = '\0';
                printf("Client %d set name to: %s\n", client_socket, clients[client_index].name);
                send_message(client_socket, MSG_OK, "Name set", 8);
            } else {
                send_message(client_socket, MSG_ERROR, "Invalid name length", 19);
            }
            break;

        case MSG_SEND_MESSAGE:
            if (strlen(clients[client_index].name) == 0) {
                send_message(client_socket, MSG_ERROR, "Set name first", 14);
            } else if (data_len > 0) {
                printf("Broadcasting message from %s: %s\n", clients[client_index].name, data);
                broadcast_message(client_socket, clients[client_index].name, data);
            }
            break;

        default:
            printf("Unknown message type %d from client %d\n", type, client_socket);
            send_message(client_socket, MSG_ERROR, "Unkown message type", 20);
            break;
    }
}


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


int main(void)
{
    int server_fd, new_socket, max_fd, activity, i, sd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    fd_set readfds;

    // Setup the server socket
    server_fd = set_up_server_socket();

    //printf("Waiting for connections...\n");

    // Main server loop
    while(1) {
        // clear the socket set
        FD_ZERO(&readfds);

        // Add server socket to the set
        FD_SET(server_fd, &readfds);
        max_fd = server_fd;

        // add client sockets to set
        for (i = 0; i < MAX_CLIENTS; i++)
        {
            sd = clients[i].socket_fd;
            if (sd > 0)
                FD_SET(sd, &readfds);

            if (sd > max_fd)
                max_fd = sd;
        }

        // Wait for activity on any socket
        activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
        }

        // if something happened on the server socket, its an incoming connection
        if (FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept error");
                exit(EXIT_FAILURE);
            }

            printf("New Connection: socket fd %d, IP: %s, PORT: %d\n", new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            // Add new socket to array of clients
            for (i = 0; i < MAX_CLIENTS; i++) {
                // search for an empty slot
                if (clients[i].socket_fd == 0) {
                    clients[i].socket_fd = new_socket;
                    clients[i].name[0] = '\0';
                    clients[i].buffer_len = 0;

                    printf("Adding to list of sockets as index %d\n", i);

                    // Send welcome message with instruction
                    send_message(new_socket, MSG_SEND_MESSAGE, "Welcome! Send a SET_NAME message to begin.", 45);
                    break;
                }
            }
        }

        // Check each client socket for IO operations
        for(i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i].socket_fd;

            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                // Process data from client using the TLV parser
                if (process_client_data(&clients[i]) == -1) {
                    // client disconnected or error occured
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("Host disconnected, IP: %s, PORT: %d, NAME: %s\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port), clients[i].name[0] ? clients[i].name : "<unamed>");

                    // close the socket and mark as 0 in list for reuse
                    close(sd);
                    clients[i].socket_fd = 0;
                    clients[i].buffer_len = 0;
                    clients[i].name[0] = '\0';
                }
            }
        }
    }

    return 0;
}
