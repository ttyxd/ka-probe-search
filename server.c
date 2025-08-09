#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // <-- ADDED THIS LINE
#include <sys/select.h>
#include <time.h>
#include <errno.h>

#define PORT 65432
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define BINDING_TIMEOUT 155 // The "secret" network timeout we want the client to find (in seconds)

typedef struct {
    int sock_fd;
    time_t last_comm_time;
} client_t;

int main() {
    int master_socket, addrlen, new_socket, activity, i, valread;
    int max_sd;
    struct sockaddr_in address;
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    client_t clients[MAX_CLIENTS] = {0};

    // Create a master socket
    if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set master socket to allow multiple connections
    int opt = 1;
    if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Type of socket created
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind the socket to localhost port 8888
    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    printf("Listener on port %d \n", PORT);
    printf("Simulating a network binding timeout of %d seconds.\n", BINDING_TIMEOUT);


    // Try to specify a maximum of 3 pending connections for the master socket
    if (listen(master_socket, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    addrlen = sizeof(address);
    puts("Waiting for connections ...");

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(master_socket, &readfds);
        max_sd = master_socket;

        // Add child sockets to set
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].sock_fd > 0) {
                FD_SET(clients[i].sock_fd, &readfds);
            }
            if (clients[i].sock_fd > max_sd) {
                max_sd = clients[i].sock_fd;
            }
        }
        
        // Timeout for select, so we can check for idle clients
        struct timeval tv = {1, 0}; // Check every 1 second

        activity = select(max_sd + 1, &readfds, NULL, NULL, &tv);

        if ((activity < 0) && (errno != EINTR)) {
            printf("select error");
        }

        // Check for idle connections and drop them
        time_t now = time(NULL);
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].sock_fd > 0 && (now - clients[i].last_comm_time > BINDING_TIMEOUT)) {
                printf("Client %d timed out (idle for > %d seconds). Closing connection.\n", clients[i].sock_fd, BINDING_TIMEOUT);
                close(clients[i].sock_fd);
                clients[i].sock_fd = 0;
            }
        }

        // If something happened on the master socket, then its an incoming connection
        if (FD_ISSET(master_socket, &readfds)) {
            if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            printf("New connection, socket fd is %d, ip is : %s, port : %d\n", new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            // Add new socket to array of clients
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].sock_fd == 0) {
                    clients[i].sock_fd = new_socket;
                    clients[i].last_comm_time = time(NULL);
                    printf("Adding to list of sockets as %d\n", i);
                    break;
                }
            }
        }

        // Else its some IO operation on some other socket
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (FD_ISSET(clients[i].sock_fd, &readfds)) {
                // Check if it was for closing, and also read the incoming message
                if ((valread = read(clients[i].sock_fd, buffer, BUFFER_SIZE)) == 0) {
                    printf("Client %d disconnected\n", clients[i].sock_fd);
                    close(clients[i].sock_fd);
                    clients[i].sock_fd = 0;
                } else {
                    buffer[valread] = '\0';
                    printf("Client %d sent: %s", clients[i].sock_fd, buffer);
                    clients[i].last_comm_time = time(NULL);
                    // Echo back the message that came in
                    write(clients[i].sock_fd, buffer, strlen(buffer));
                }
            }
        }
    }

    return 0;
}
