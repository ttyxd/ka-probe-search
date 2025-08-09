#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include "composite_search.h"

#define SERVER_PORT 8888
#define BUFFER_SIZE 1024
#define INITIAL_KA_INTERVAL 15 // a safe, initial keep-alive for the main connection

// creates a non-blocking socket and connects to the server.
int create_and_connect(const char* server_ip) {
    int sock_fd;
    struct sockaddr_in serv_addr;

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }

    // use a blocking connect, as it's simpler to manage.
    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        // this is expected during reconnect attempts, so don't print perror every time.
        return -1;
    }
    
    // set to non-blocking after connection is established.
    fcntl(sock_fd, F_SETFL, O_NONBLOCK);
    return sock_fd;
}

int main(int argc, char const *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Server IP>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char* server_ip = argv[1];

    int main_socket, test_socket;
    char buffer[BUFFER_SIZE];

    printf("--- Main Connection ---\n");
    main_socket = create_and_connect(server_ip);
    printf("--- Test Connection ---\n");
    test_socket = create_and_connect(server_ip);

    if (main_socket < 0 || test_socket < 0) {
        fprintf(stderr, "Initial connection failed. Exiting.\n");
        exit(EXIT_FAILURE);
    }
    
    printf("\nConnections established. Starting Composite Search...\n\n");

    search_state_t search_state;
    search_init(&search_state, 2, 1200, 4, INITIAL_KA_INTERVAL);
    
    int main_ka_interval = INITIAL_KA_INTERVAL;
    time_t main_last_comm = time(NULL);
    time_t test_last_comm = time(NULL);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(main_socket, &readfds);
        FD_SET(test_socket, &readfds);
        int max_sd = (main_socket > test_socket) ? main_socket : test_socket;

        time_t now = time(NULL);
        time_t main_next_ka_time = main_last_comm + main_ka_interval;
        time_t test_next_ka_time = test_last_comm + search_state.current_probe_interval;

        long main_wait = main_next_ka_time - now;
        long test_wait = test_next_ka_time - now;
        
        if (main_wait < 0) main_wait = 0;
        if (test_wait < 0) test_wait = 0;

        // use the shorter of the two wait times for the select timeout.
        struct timeval tv;
        tv.tv_sec = (main_wait < test_wait) ? main_wait : test_wait;
        tv.tv_usec = 0;

        int activity = select(max_sd + 1, &readfds, NULL, NULL, &tv);

        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
            break;
        }
        
        now = time(NULL);

        // --- handle incoming data ---
        if (FD_ISSET(main_socket, &readfds)) {
            if (read(main_socket, buffer, BUFFER_SIZE) <= 0) {
                // --- LEVEL 3 FAILURE RECOVERY ---
                printf("\n[FATAL] Main connection dropped! Initiating full state reset.\n");
                close(main_socket);

                // 1. attempt to reconnect the main channel
                while (1) {
                    printf("[RECONNECT] Attempting to re-establish main connection...\n");
                    main_socket = create_and_connect(server_ip);
                    if (main_socket > 0) {
                        printf("[RECONNECT] Main connection re-established.\n");
                        break;
                    }
                    sleep(5); // wait 5 seconds before retrying
                }

                // 2. reset main channel state
                main_ka_interval = INITIAL_KA_INTERVAL;
                main_last_comm = time(NULL);
                printf("[RESET] Main keep-alive interval reset to %ds.\n", main_ka_interval);

                // 3. reset test channel and search algorithm
                printf("[RESET] Re-establishing test connection and resetting search algorithm.\n");
                close(test_socket);
                test_socket = create_and_connect(server_ip);
                if (test_socket < 0) {
                    fprintf(stderr, "Could not re-establish test connection. Exiting.\n");
                    exit(EXIT_FAILURE);
                }
                search_init(&search_state, 2, 1200, 4, INITIAL_KA_INTERVAL);
                test_last_comm = time(NULL);
                printf("[RESET] System state fully reset. Resuming normal operation.\n\n");
                continue; // restart the main loop
            } else {
                main_last_comm = now;
                printf("[MAIN] Keep-Alive echo received.\n");
            }
        }

        if (FD_ISSET(test_socket, &readfds)) {
            if (read(test_socket, buffer, BUFFER_SIZE) > 0) {
                test_last_comm = now;
                search_handle_success(&search_state);
                // if the search finds a better interval, apply it to the main connection.
                if (search_state.last_successful_interval > main_ka_interval) {
                    main_ka_interval = search_state.last_successful_interval;
                    printf("[MAIN] UPDATED Keep-Alive interval to %ds.\n", main_ka_interval);
                }
            } else { // connection was dropped.
                printf("[TEST] Connection dropped, probe failed.\n");
                search_handle_failure(&search_state);
                printf("[TEST] Re-establishing test connection...\n");
                close(test_socket);
                test_socket = create_and_connect(server_ip);
                if (test_socket < 0) {
                    printf("Failed to re-establish test connection. Exiting.\n");
                    break;
                }
                test_last_comm = time(NULL);
            }
        }

        // --- send keep-alives if needed ---
        if (now >= main_next_ka_time) {
            if (write(main_socket, "ka_main\n", 8) <= 0) {
                // write can also fail if connection is dropped, trigger recovery here too
                // by letting the read handler in the next loop iteration catch it.
                printf("[MAIN] Write failed. Connection likely dropped.\n");
            } else {
                printf("[MAIN] Sending Keep-Alive (interval: %ds).\n", main_ka_interval);
                main_last_comm = now;
            }
        }
        if (now >= test_next_ka_time) {
             if (write(test_socket, "ka_test\n", 8) <= 0) {
                printf("[TEST] Write failed. Connection likely dropped.\n");
             } else {
                printf("[TEST] Sending KA Probe (interval: %ds).\n", search_state.current_probe_interval);
                test_last_comm = now;
             }
        }
    }

    close(main_socket);
    close(test_socket);
    return 0;
}
