// test_client_v1.c - Simple client to test v1.0 server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>

#define PORT 8080
#define SERVER_IP "127.0.0.1"

#pragma pack(push, 1)
typedef struct {
    uint8_t type;
    uint32_t length;
} message_header_t;
#pragma pack(pop)

void send_tlv_message(int sock, uint8_t type, const char *data) {
    message_header_t header;
    header.type = type;
    header.length = htonl(strlen(data));
    
    printf("CLIENT DEBUG: Sending header: type=0x%02x, length=%u (0x%08x)\n", 
           type, ntohl(header.length), header.length);
    printf("CLIENT DEBUG: Data: '%s' (%zu bytes)\n", data, strlen(data));
    
    int header_sent = send(sock, &header, sizeof(header), 0);
    int data_sent = send(sock, data, strlen(data), 0);
    
    printf("CLIENT DEBUG: Sent %d + %d = %d bytes total\n\n", 
           header_sent, data_sent, header_sent + data_sent);
    
    sleep(1); // Add delay between messages
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);
    
    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    printf("Connected to server. Sending test messages...\n");
    
    // Test sequence with delays
    send_tlv_message(sock, 0x01, "Alice");  // SET_NAME
    send_tlv_message(sock, 0x02, "Hello from secure client!");  // SEND_MESSAGE
    
    // Wait to receive any responses
    printf("Waiting for server responses...\n");
    sleep(3);
    
    char buffer[1024];
    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Received from server: %d bytes\n", bytes_received);
    }
    
    close(sock);
    printf("Client finished.\n");
    return 0;
}
