#ifndef _SKEL_H_
#define _SKEL_H_

#define DIE(condition, message, ...) \
	do { \
		if ((condition)) { \
			fprintf(stderr, "[(%s:%d)]: " # message "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
			perror(""); \
			exit(1); \
		} \
	} while (0)

#define PAYLOAD_MAX_LEN 1500
#define PACKET_MAX_LEN 1700
#define TOPIC_MAX_LEN 50
#define MAX_PORT 65000
#define ID_TYPE 4
#define SUBSCRIBE_TYPE 5
#define UNSUBSCRIBE_TYPE 6


// receive an entire message from fd. bytes_recv will contain number of bytes received
char *recv_all(int fd, size_t& bytes_recv);

// send an entire message to fd
// ! message_length must be in host order
void send_all(int fd, char *buffer, size_t message_length);

/*
	returns a char array ready to be sent to the server
 	message format is LENGTH | TYPE | DATA 
 	! length must be in host order 
*/
char *make_message_client(uint32_t length, uint8_t type, char *data);


#endif /* _SKEL_H_ */
