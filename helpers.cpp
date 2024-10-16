#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <sys/types.h>
#include "helpers.h"

using namespace std;

char *recv_all(int fd, size_t& bytes_recv)
{
    char *buffer = (char *)calloc(PACKET_MAX_LEN, sizeof(char));
    DIE(buffer == NULL, "Failed to allocate memory\n");
    // first we receive length of the message, 
    // to know how much we need to receive
    size_t bytes_received = 0;
    size_t bytes_to_receive = sizeof(uint32_t);
    ssize_t received = recv(fd, buffer, bytes_to_receive, 0);
    DIE(received == -1, "recv all failed.\n");
    if (received == 0) {
        return NULL;
    }
    bytes_recv += received;

    // now we need to receive length - sizeof(length) bytes
    bytes_received = 0;
    size_t message_length = 0;
    memcpy(&message_length, buffer, sizeof(uint32_t));
    message_length = ntohl(message_length);
    bytes_to_receive = message_length - sizeof(uint32_t);
    while (bytes_to_receive) {
        ssize_t received = recv(fd, buffer + sizeof(uint32_t) + bytes_received, bytes_to_receive, 0);
        DIE(received == -1, "recv all failed.\n");
        bytes_received += received;
        bytes_to_receive -= received;
    }
    bytes_recv += bytes_received;
    return buffer;
}

void send_all(int fd, char *buffer, size_t message_length)
{
    // we need to send the entire buffer
    size_t bytes_sent = 0;
    size_t bytes_to_send = message_length;
    while (bytes_to_send) {
        ssize_t sent = send(fd, buffer + bytes_sent, bytes_to_send, 0);
        DIE(sent == -1, "send all failed.\n");
        bytes_sent += sent;
        bytes_to_send -= sent;
    }
}

char *make_message_client(uint32_t length, uint8_t type, char *data)
{
    char *buffer = (char *)calloc(PACKET_MAX_LEN, sizeof(char));
    DIE(buffer == NULL, "Failed to allocate memory.\n");
    length = htonl(length);
    memcpy(buffer, &length, sizeof(uint32_t));
    memcpy(buffer + sizeof(uint32_t), &type, sizeof(uint8_t));
    memcpy(buffer + sizeof(uint32_t) + sizeof(uint8_t), data, strlen(data));
    return buffer;
}