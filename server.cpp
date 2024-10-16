#include <iostream>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <string>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <unordered_map>
#include <bits/stdc++.h>
#include <math.h>
#include <vector>
#include "helpers.h"

using namespace std;

bool check_wildcard_topic(string subscribed, string received_topic) {
    char *wildcard_topic = strdup(subscribed.c_str());
    char *searched_topic = strdup(received_topic.c_str());

    char *save_wildcard, *save_topic;
    char *token_wildcard = strtok_r(wildcard_topic, "/", &save_wildcard);
    char *token_topic = strtok_r(searched_topic, "/", &save_topic);

    while (1) {
        if (!strcmp(token_wildcard, "+")) {
            // go 1 more level in each topic
            token_wildcard = strtok_r(NULL, "/", &save_wildcard);
            token_topic = strtok_r(NULL, "/", &save_topic);

            // both ended, return true
            if (token_wildcard == NULL && token_topic == NULL) {
                return true;
            } else if (token_wildcard == NULL || token_topic == NULL) {
                // if only one of them is NULL, no match was found
                return false;
            }

            // if they are not equal, return false
            if (strcmp(token_wildcard, token_topic) && strcmp(token_wildcard, "*")) {
                return false;
            }
            continue;
        }

        if (!strcmp(token_wildcard, "*")) {
            // go one more level in both
            token_wildcard = strtok_r(NULL, "/", &save_wildcard);
            token_topic = strtok_r(NULL, "/", &save_topic);

            // if wildcard ended, it will match anything
            if (!token_wildcard) {
                return true;
            } else if (!token_topic) {
                return false;
            }

            // go deeper until we found a match or token becomes NULL
            while (token_topic != NULL) {
                if (!strcmp(token_wildcard, token_topic)) {
                    break;
                }
                token_topic = strtok_r(NULL, "/", &save_topic);
            }
            continue;
        }

        if (token_wildcard == NULL && token_topic == NULL) {
            return true;
        } else if (token_wildcard == NULL || token_topic == NULL) {
            return false;
        }

        // if they are different, return false
        if (strcmp(token_wildcard, token_topic)) {
            return false;
        }

        // go to the next pair
        token_wildcard = strtok_r(NULL, "/", &save_wildcard);
        token_topic = strtok_r(NULL, "/", &save_topic);

        if (token_wildcard == NULL && token_topic == NULL) {
            return true;
        } else if (token_wildcard == NULL || token_topic == NULL) {
            return false;
        }
    }

    return false;
}

int get_payload_size(uint8_t type) {
    switch (type)
    {
        case 0:
            // INT
            return sizeof(uint32_t) + sizeof(uint8_t);
        break;

        case 1:
            // SHORT_REAL
            return sizeof(uint16_t);
        break;

        case 2:
            // FLOAT
            return sizeof(uint32_t) + 2 * sizeof(uint8_t);
        break;

        case 3:
            // STRING
            return PAYLOAD_MAX_LEN;
        break;

        default:
            return -1;
        break;
    }
}

struct pollfd *insert_fd(struct pollfd *fds, int newsockfd, int& fds_current_size, int& max_size)
{
    if (fds_current_size == max_size - 1) {
        struct pollfd *new_fds = (struct pollfd *)calloc(max_size * 2, sizeof(new_fds));
        DIE(new_fds == NULL, "Failed to allocate memory.\n");
        for (int i = 0; i < fds_current_size; i++) {
            new_fds[i] = fds[i];
        }
        new_fds[fds_current_size].fd = newsockfd;
        new_fds[fds_current_size].events = POLLIN;
        fds_current_size++;
        max_size *= 2;
        free(fds);
        return new_fds;
    }

    fds[fds_current_size].fd = newsockfd;
    fds[fds_current_size].events = POLLIN;
    fds_current_size++;

    return fds;
}

// main logic for server implementation
void start_server(struct pollfd *fds, int udp_socket, int tcp_socket)
{
    // keep all online users -> ID, fd
    unordered_map<string, int> online_clients;

    // keep fd to ID mapping. Needed for disconnect
    unordered_map<int, string> fds_to_id;

    // keep what topics (not containing wildcards) an user is subscribed to
    unordered_map<string, unordered_set<string>> simple_topics_subscribes;

    // keep what topics (with wildcards) an user is subscribed to
    unordered_map<string, unordered_set<string>> wildcard_topics_subscribes;

    int fds_size = 3, rc, max_size = 10;
    while (1) {
        rc = poll(fds, fds_size, 0);
        if (rc < 0) {
            perror("Poll failed.\n");
            for (int j = 0; j < fds_size; j++) {
                close(fds[j].fd);
            }
            exit(1);
        }

        for (int i = 0; i < fds_size; i++) {
            int pollin_happened = fds[i].revents & POLLIN;
            if (!pollin_happened) {
                continue;
            }
            // read data from standard input
            if (fds[i].fd == 0) {
                char *buffer = (char *)malloc(100);
                DIE(buffer == NULL, "Failed to allocate memory.\n");
                fgets(buffer, 100, stdin);
                if (strcmp(buffer, "exit\n")) {
                    cerr << "Invalid command was ignored.\n";
                } else {
                    for (int j = 0; j < fds_size; j++) {
                        close(fds[i].fd);
                    }
                    return;
                }
                free(buffer);
            } else if (fds[i].fd == udp_socket) {
                char *buffer = (char *)calloc(PACKET_MAX_LEN, sizeof(char));
                DIE(buffer == NULL, "Failed to allocate memory.\n");

                struct sockaddr_in udp_info;
                socklen_t udp_len = sizeof(udp_info);
                rc = recvfrom(udp_socket, buffer, PACKET_MAX_LEN, 0, (struct sockaddr *)&udp_info, &udp_len);

                if (rc < 0) {
                    perror("Failed recv.\n");
                    exit(1);
                }

                // length = length + ip + port + topic (50) + type + payload
                uint32_t length = 0;
                // get port and ip of sender (network byte order)
                uint16_t port = udp_info.sin_port;
                uint32_t ip_udp = udp_info.sin_addr.s_addr;

                // create message to send to TCP clients
                char *new_message = (char *)calloc(PACKET_MAX_LEN, sizeof(char));

                // get topic from udp message
                char topic[TOPIC_MAX_LEN + 1];
                memcpy(topic, buffer, TOPIC_MAX_LEN);
                topic[TOPIC_MAX_LEN] = '\0';

                // get data type
                uint8_t type = 0;
                memcpy(&type, buffer + TOPIC_MAX_LEN, sizeof(uint8_t));

                // get payload
                char payload[PAYLOAD_MAX_LEN + 1];
                int payload_size = get_payload_size(type);
                if (payload_size == -1) {
                    break;
                }

                // check if we got a number
                if (payload_size != PAYLOAD_MAX_LEN) {
                    memcpy(payload, buffer + TOPIC_MAX_LEN + sizeof(uint8_t), payload_size);
                } else {
                    // copy the entire string
                    payload_size = 0;
                    for (int i = 0; i < PAYLOAD_MAX_LEN; i++) {
                        payload_size++;
                        payload[i] = (buffer + TOPIC_MAX_LEN + sizeof(uint8_t))[i];
                        if (payload[i] == '\0') {
                            break;
                        }
                    }
                }

                length = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t) +
                            TOPIC_MAX_LEN + sizeof(uint8_t) + payload_size;
                length = htonl(length);

                // put data in message
                int offset = 0;
                memcpy(new_message, &length, sizeof(uint32_t));
                offset += sizeof(uint32_t);

                memcpy(new_message + offset, &ip_udp, sizeof(uint32_t));
                offset += sizeof(uint32_t);

                memcpy(new_message + offset, &port, sizeof(uint16_t));
                offset += sizeof(uint16_t);

                memcpy(new_message + offset, topic, TOPIC_MAX_LEN);
                offset += TOPIC_MAX_LEN;

                memcpy(new_message + offset, &type, sizeof(uint8_t));
                offset += sizeof(uint8_t);

                memcpy(new_message + offset, payload, payload_size);

                // send data to TCP clients that are subscribed to the topic
                string searched_topic(topic);
                for (int k = 3; k < fds_size; k++) {
                    string id_client = fds_to_id[fds[k].fd];
                    auto simple_it = simple_topics_subscribes[id_client].find(searched_topic);

                    // check if it matches any simple topic
                    if (simple_it != simple_topics_subscribes[id_client].end()) {
                        send_all(fds[k].fd, new_message, ntohl(length));
                        continue;
                    }

                    auto wildcard_it = wildcard_topics_subscribes[id_client].begin();

                    // check if it matches any wildcard topic
                    for (; wildcard_it != wildcard_topics_subscribes[id_client].end(); wildcard_it++) {
                        bool topic_matches = check_wildcard_topic(*wildcard_it, searched_topic);
                        if (topic_matches) {
                            send_all(fds[k].fd, new_message, ntohl(length));
                            break;
                        }
                    }
                }
                break;

            } else if (fds[i].fd == tcp_socket) {
                // we got a new connection

                struct sockaddr_in new_client;
                socklen_t new_client_len = sizeof(new_client);
                const int newsockfd = accept(tcp_socket, (struct sockaddr *)&new_client, &new_client_len);
                DIE(newsockfd < 0, "accept");

                // disable nagle for the new socket
                const int enable = 1;
                if(setsockopt(newsockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &enable, sizeof(int)) < 0) {
                    perror("setsockopt(TCP_NODELAY) failed.\n");
                    exit(1);
                }

                // wait for the new client to send his ID
                size_t received = 0;
                char *buffer = recv_all(newsockfd, received); 
                // extract length
                uint32_t length = 0;
                memcpy(&length, buffer, sizeof(uint32_t));
                length = ntohl(length);
                // extract type
                uint8_t type = 0;
                memcpy(&type, buffer + sizeof(uint32_t), sizeof(uint8_t));
                // extract ID
                string client_id = string(buffer + sizeof(uint32_t) + sizeof(uint8_t),
                                            length - sizeof(uint8_t) - sizeof(uint32_t));

                // check if client is already online
                if (online_clients.find(client_id) != online_clients.end()) {
                    printf("Client %s already connected.\n", client_id.c_str());
                    close(newsockfd);
                    continue;
                }

                // if client is not online, add it to fds and to hashmap
                online_clients[client_id] = newsockfd;
                fds_to_id[newsockfd] = client_id;
                fds = insert_fd(fds, newsockfd, fds_size, max_size);

                struct in_addr client_address;
                client_address.s_addr = new_client.sin_addr.s_addr;
                printf("New client %s connected from %s:%d.\n", client_id.c_str(), inet_ntoa(client_address), ntohs(new_client.sin_port));

                break;
            } else {
                // we got data from a client
                size_t received = 0;
                char *buffer = recv_all(fds[i].fd, received);
                if (received == 0) {
                    // client closed connection, remove from list
                    printf("Client %s disconnected.\n", fds_to_id[fds[i].fd].c_str());
                    int key_fds = fds[i].fd;
                    string online_key = fds_to_id.find(key_fds)->second;

                    auto iterator_online = online_clients.find(online_key);
                    auto iterator_fds = fds_to_id.find(key_fds);

                    if (iterator_online != online_clients.end() && iterator_fds != fds_to_id.end()) {
                        online_clients.erase(iterator_online);
                        fds_to_id.erase(iterator_fds);
                    }

                    close(fds[i].fd);
                    fds[i] = fds[fds_size - 1];
                    fds_size--;
                    break;
                }

                // extract length
                uint32_t length = 0;
                memcpy(&length, buffer, sizeof(uint32_t));
                length = ntohl(length);
                // extract type
                uint8_t type = 0;
                memcpy(&type, buffer + sizeof(uint32_t), sizeof(uint8_t));
                // extract topic
                char char_topic[TOPIC_MAX_LEN + 1];
                memcpy(char_topic, buffer + sizeof(uint8_t) + sizeof(uint32_t), length - sizeof(uint8_t) - sizeof(uint32_t));
                char_topic[TOPIC_MAX_LEN] = '\0'; 
                string topic(char_topic);

                bool contains_wildcard = topic.find("*") != topic.npos 
                                        || topic.find("+") != topic.npos;

                int key_fds = fds[i].fd;
                // ID of client that gave the command
                string id_user = fds_to_id.find(key_fds)->second;

                bool has_subscribes = false;
                if (simple_topics_subscribes.find(id_user) != simple_topics_subscribes.end()
                    || wildcard_topics_subscribes.find(id_user) != wildcard_topics_subscribes.end()) {
                    has_subscribes = true;
                }

                switch (type)
                {
                    case 5:
                        // SUBSCRIBE
                        // if user is not subscribed to any topic, update hashmap
                        if (!has_subscribes && !contains_wildcard) {
                            unordered_set<string> new_set;
                            new_set.insert(topic);
                            simple_topics_subscribes.insert({id_user, new_set});
                            break;
                        } else if(!has_subscribes && contains_wildcard) {
                            unordered_set<string> new_set;
                            new_set.insert(topic);
                            wildcard_topics_subscribes.insert({id_user, new_set});
                            break;
                        }

                        if (!contains_wildcard) {
                            simple_topics_subscribes[id_user].insert(topic);
                        } else {
                            wildcard_topics_subscribes[id_user].insert(topic);
                        }
                    break;

                    case 6:
                        if (!has_subscribes) {
                            break;
                        }
                        if (!contains_wildcard) {
                            simple_topics_subscribes[id_user].erase(topic);
                        } else {
                            wildcard_topics_subscribes[id_user].erase(topic);
                        }
                    break;
                }
                break;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    if (argc != 2) {
        printf("Usage: %s <PORT>\n", argv[0]);
        exit(1);
    }

    // Get port as number
    uint16_t port;
    int rc = sscanf(argv[1], "%hu", &port);
    DIE(rc != 1, "Given port is invalid.\n");

    int tcp_listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    DIE(tcp_listening_socket < 0, "Could not open socket.\n");

    struct sockaddr_in serv_addr;
    socklen_t socket_len = sizeof(struct sockaddr_in);

    // make socket reusable
    const int enable = 1;
    if (setsockopt(tcp_listening_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(1);
    }

    // disable Nagle
    if(setsockopt(tcp_listening_socket, IPPROTO_TCP, TCP_NODELAY, (char *) &enable, sizeof(int)) < 0) {
        perror("setsockopt(TCP_NODELAY) failed.\n");
        exit(1);
    }

    memset(&serv_addr, 0, socket_len);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    
    rc = bind(tcp_listening_socket, (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
    DIE(rc < 0, "Failed to bind tcp socket");

    int udp_listening_socket = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(udp_listening_socket < 0, "Could not open socket.\n");

    // make socket reusable
    if (setsockopt(udp_listening_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(1);
    }
    
    rc = bind(udp_listening_socket, (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
    DIE(rc < 0, "Failed to bind tcp socket");

    rc = listen(tcp_listening_socket, 10);
    DIE(rc < 0, "Failed to listen on TCP.\n");

    struct pollfd *fds = (struct pollfd *)calloc(10, sizeof(struct pollfd));
    DIE(fds == NULL, "Error allocating memory\n");

    // monitor standard input
    fds[0].fd = 0;
    fds[0].events = POLLIN;

    // monitor TCP listening socket
    fds[1].fd = tcp_listening_socket;
    fds[1].events = POLLIN;

    // monitor UDP listening socket
    fds[2].fd = udp_listening_socket;
    fds[2].events = POLLIN;

    start_server(fds, udp_listening_socket, tcp_listening_socket);
    close(tcp_listening_socket);
    close(udp_listening_socket);

    return 0;
}