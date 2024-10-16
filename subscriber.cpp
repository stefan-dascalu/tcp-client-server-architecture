#include <iostream>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include "helpers.h"

using namespace std;

bool is_valid(char *topic) {
    /* 
        wildcards should be used alone : upb/precis/+
        this is not valid : upb/precis+
    */
    char *copy = strdup(topic);
    char *token = strtok(copy, "/");
    while (token != NULL) {
        if (strchr(token, '+') != NULL && strcmp(token, "+")) {
            return false;
        }

        if (strchr(token, '*') != NULL && strcmp(token, "*")) {
            return false;
        }

        token = strtok(NULL, "/");
    }
    return true;
}

// reads topic from stdin command
char *get_topic_stdin(char *buffer) {
    // limit topic to 50 characters
    buffer[TOPIC_MAX_LEN] = '\0';
    int size = strlen(buffer);

    char *topic = (char *)calloc(TOPIC_MAX_LEN, sizeof(char));
    DIE(topic == NULL, "Failed to allocate memory.\n");

    bool found_letter = false;
    for (int i = size - 1; i >= 0; i--) {
        bool is_allowed = isalnum(buffer[i]) || buffer[i] == '/'
                        || buffer[i] == '+' || buffer[i] == '*' || buffer[i] == '_';

        // check if we found the first letter
        if (is_allowed) {
            found_letter = true;
        }

        bool is_space = buffer[i] == ' ' || buffer[i] == '\t' ? true : false;

        if (found_letter && is_space) {
            // topic contains whitespace inside
            free(topic);
            return NULL;
        } else if (!is_allowed && !is_space && found_letter) {
            // if we found a character that is not in the allowed characters list
            free(topic);
            return NULL;
        } else if (is_allowed){
            topic[i] = buffer[i];
        }
    }
    return topic;
}

// main logic for client
void start_client(struct pollfd fds[2])
{
    int fds_size = 2, rc;
    while (1) {
        rc = poll(fds, fds_size, 0);
        if (rc < 0) {
            perror("Poll failed.\n");
            return;
        }

        for (int i = 0; i < fds_size; i++) {
            int pollin_happened = fds[i].revents & POLLIN;
            if (!pollin_happened) {
                continue;
            }

            // read data from standard input
            if (i == 0) {
                char *input = (char *)malloc(100);
                DIE(input == NULL, "Failed to allocate memory.\n");
                fgets(input, 100, stdin);
                if (!strcmp(input, "exit\n")) {
                    free(input);
                    return;
                }

                // if we want to subscribe
                if (!strncmp(input, "subscribe ", 10)) {
                    char *topic = get_topic_stdin(input + 10);
                    if (topic == NULL || !is_valid(topic)) {
                        cerr << "Invalid topic ignored.\n";
                        break;
                    }

                    uint32_t length = strlen(input) + sizeof(uint32_t) + sizeof(uint8_t);
                    char *buffer = make_message_client(length, SUBSCRIBE_TYPE, topic);
                    send_all(fds[1].fd, buffer, length);

                    printf("Subscribed to topic %s\n", topic);
                } else if (!strncmp(input, "unsubscribe ", 12)) {
                    char *topic = get_topic_stdin(input + 12);
                    if (topic == NULL || !is_valid(topic)) {
                        cerr << "Invalid topic ignored.\n";
                        break;
                    }

                    uint32_t length = strlen(input) + sizeof(uint32_t) + sizeof(uint8_t);
                    char *buffer = make_message_client(length, UNSUBSCRIBE_TYPE, topic);
                    send_all(fds[1].fd, buffer, length);

                    printf("Unsubscribed from topic %s\n", topic);
                } else {
                    cerr << "Invalid command was ignored.\n";
                }

                free(input);
            } else {
                size_t received = 0;
                char *buffer = recv_all(fds[i].fd, received);
                // check if server closed connection
                if (received == 0) {
                    return;
                }

                // extract data from packet
                int offset = sizeof(uint32_t);
                uint32_t ip_udp;
                uint16_t port_udp;

                memcpy(&ip_udp, buffer + offset, sizeof(uint32_t));
                offset += sizeof(uint32_t);

                memcpy(&port_udp, buffer + offset, sizeof(uint16_t));
                offset += sizeof(uint16_t);

                port_udp = ntohs(port_udp);

                struct in_addr sender_address;
                sender_address.s_addr = ip_udp;

                char topic[51];
                memcpy(topic, buffer + offset, TOPIC_MAX_LEN);
                topic[50] = '\0';
                offset += TOPIC_MAX_LEN;

                uint8_t type;
                memcpy(&type, buffer + offset, sizeof(uint8_t));
                offset += sizeof(uint8_t);

                switch (type)
                {
                    case 0: {
                        // INT
                        uint8_t sign_byte;
                        uint32_t integer;
                        memcpy(&sign_byte, buffer + offset, sizeof(uint8_t));
                        offset += sizeof(uint8_t);
                        memcpy(&integer, buffer + offset, sizeof(uint32_t));
                        integer = ntohl(integer);
                        if (sign_byte == 1) {
                            integer = -integer;
                        }
                        printf("%s:%d - %s - INT - %d\n", inet_ntoa(sender_address), port_udp, topic, integer);
                        break;
                    }

                    case 1: {
                        // SHORT_REAL
                        uint16_t short_real;
                        memcpy(&short_real, buffer + offset, sizeof(uint16_t));
                        short_real = ntohs(short_real);
                        double result = (1.0 * short_real / 100);
                        printf("%s:%d - %s - SHORT_REAL - %.2f\n", inet_ntoa(sender_address), port_udp, topic, result);
                        break;
                    }

                    case 2: {
                        // FLOAT
                        uint8_t sign_byte;
                        uint32_t integer;
                        uint8_t negative_power;
                        memcpy(&sign_byte, buffer + offset, sizeof(uint8_t));
                        offset += sizeof(uint8_t);
                        memcpy(&integer, buffer + offset, sizeof(uint32_t));
                        integer = ntohl(integer);
                        offset += sizeof(uint32_t);
                        memcpy(&negative_power, buffer + offset, sizeof(uint8_t));

                        double result = integer * pow(10, -negative_power);
                        if (sign_byte == 1) {
                            result = - result;
                        }
                        printf("%s:%d - %s - FLOAT - %lf\n", inet_ntoa(sender_address), port_udp, topic, result);
                        break;
                    }

                    case 3: {
                        // STRING
                        char *result = (char *)calloc(PAYLOAD_MAX_LEN + 1, sizeof(char));
                        int i = 0;
                        for (i = 0; i < PAYLOAD_MAX_LEN; i++) {
                            result[i] = (buffer + offset)[i];
                            if (result[i] == '\0') {
                                break;
                            }
                        }
                        result[i] = '\0';
                        printf("%s:%d - %s - STRING - %s\n", inet_ntoa(sender_address), port_udp, topic, result);
                        break;
                    }
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    if (argc != 4) {
        printf("Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n", argv[0]);
        exit(1);
    }

    if (strlen(argv[1]) > 10) {
        printf("Client ID must be shorter than 10 characters.\n");
        exit(1);
    }

    // Get port as number
    uint16_t port;
    int rc = sscanf(argv[3], "%hu", &port);
    DIE(rc != 1, "Given port is invalid.\n");

    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    DIE(tcp_socket < 0, "Could not open socket.\n");

    struct sockaddr_in server_addr;
    socklen_t socket_len = sizeof(struct sockaddr_in);

    // make socket reusable
    const int enable = 1;
    if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed.\n");
        exit(1);
    }

    if(setsockopt(tcp_socket, IPPROTO_TCP, TCP_NODELAY, (char *) &enable, sizeof(int)) < 0) {
        perror("setsockopt(TCP_NODELAY) failed.\n");
        exit(1);
    }

    memset(&server_addr, 0, socket_len);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    rc = inet_pton(AF_INET, argv[2], &server_addr.sin_addr.s_addr);
    DIE(rc <= 0, "inet_pton.\n");

    struct pollfd fds[2];
    DIE(fds == NULL, "Error allocating memory\n");

    rc = connect(tcp_socket, (struct sockaddr *)&server_addr, socket_len);
    DIE(rc < 0, "Connect failed.\n");

    // send client ID to server
    // message format: length (uint32_t) | type (4) (uint8_t) | CLIENT ID (char[])
    uint32_t message_length = sizeof(uint32_t) + sizeof(uint8_t) + strlen(argv[1]);
    char *buffer = make_message_client(message_length, ID_TYPE, argv[1]);
    send_all(tcp_socket, buffer, message_length);
    
    // monitor standard input
    fds[0].fd = 0;
    fds[0].events = POLLIN;

    // monitor TCP socket
    fds[1].fd = tcp_socket;
    fds[1].events = POLLIN;

    start_client(fds);
    close(tcp_socket);

    return 0;
}