#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <limits.h> // to check int input for id
#include "msg.h"

#define BUF 256

void Usage(char *progname);

int LookupName(char *name,
                unsigned short port,
                struct sockaddr_storage *ret_addr,
                size_t *ret_addrlen);

int Connect(const struct sockaddr_storage *addr,
             const size_t addrlen,
             int *ret_fd);

void sendQuitOperation(int socket_fd); // Handling for quit
void sendPutOperation(int socket_fd); // Handling for put
void sendGetOperation(int socket_fd); // Handling for get

int 
main(int argc, char **argv) {
  if (argc != 3) {
    Usage(argv[0]);
  }

  unsigned short port = 0;
  if (sscanf(argv[2], "%hu", &port) != 1) {
    Usage(argv[0]);
  }

  // Get an appropriate sockaddr structure.
  struct sockaddr_storage addr;
  size_t addrlen;
  if (!LookupName(argv[1], port, &addr, &addrlen)) {
    Usage(argv[0]);
  }

  // Connect to the remote host.
  int socket_fd;
  if (!Connect(&addr, addrlen, &socket_fd)) {
    Usage(argv[0]);
  }

  int userChoice;

  while(1) { // Will loop until user chooses 0
    printf("Enter your choice (1 to put, 2 to get, 0 to quit): ");
    if (scanf("%d", &userChoice) != 1) {
      printf("Invalid input. Please enter a number.\n");

      while (getchar() != '\n');
        continue;
    }

    switch(userChoice) {
      case 0: sendQuitOperation(socket_fd);
        break;
      case 1: sendPutOperation(socket_fd);
        break;
      case 2: sendGetOperation(socket_fd);
        break;
      default:
        printf("Invalid choice. Please enter 0, 1, or 2.\n");
	continue;
    }
  }

  // Clean up is done in quit operation.
  return EXIT_SUCCESS;
}

void
sendQuitOperation(int socket_fd) { // Handling for quit
  struct msg message;
  message.type = 0;
  //printf("Sending QUIT message...\n");
  write(socket_fd, &message, sizeof(struct msg)); // Let server know client is quitting
  
  close(socket_fd); // clean up
  exit(EXIT_SUCCESS); // Exit out of program after this
}


void
sendPutOperation(int socket_fd) { // Handling for put
  char id_input[MAX_NAME_LENGTH]; // will be converted to long after input
  char *endptr;
  struct msg message, response;
  message.type = PUT;
  
  while (getchar() != '\n' && !feof(stdin));

  printf("Enter the name: ");
  if (fgets(message.rd.name, MAX_NAME_LENGTH, stdin) == NULL) {
    perror("fgets");
    return;
  }
  
  if (message.rd.name[strlen(message.rd.name) - 1] == '\n') {
    message.rd.name[strlen(message.rd.name) - 1] = '\0';
  }

    printf("Enter the id: ");
    if (fgets(id_input, sizeof(id_input), stdin) == NULL) { // check number was valid
      perror("fgets");
      return;
    }
  
    message.rd.id = strtol(id_input, &endptr, 10); // convert to digit
    
    //printf("Sending PUT message...\n");
    write(socket_fd, &message, sizeof(struct msg)); // Send name/id to server
    read(socket_fd, &response, sizeof(struct msg)); // Read if put was successfull

    if (response.type == SUCCESS) {
      printf("Put success.\n");
    } else {
      printf("Put failed.\n");
    }
}

void
sendGetOperation(int socket_fd) { // Handling for get
  struct msg message, response;
  char id_input[MAX_NAME_LENGTH];
  char *endptr;

  message.type = GET;

  while (getchar() != '\n' && !feof(stdin));

  printf("Enter the id: ");
  if (fgets(id_input, sizeof(id_input), stdin) == NULL) {
    perror("fgets");
    return;
  }

  if (id_input[strlen(id_input) - 1] == '\n') {
    id_input[strlen(id_input) - 1] = '\0';
  }

  message.rd.id = strtol(id_input, &endptr, 10);

  if (*endptr != '\0') { // Check if id was valid
    // printf("Id not valid.\n");
    return;
  }

  write(socket_fd, &message, sizeof(struct msg)); // Send id to server
  read(socket_fd, &response, sizeof(struct msg)); // Read what server found

  if (response.type == SUCCESS) { // Print results of server lookup
    printf("name: %s\nid: %d\n", response.rd.name, response.rd.id);
  } else {
    printf("Record not found.\n");
  }
}

void 
Usage(char *progname) {
  printf("usage: %s  hostname port \n", progname);
  exit(EXIT_FAILURE);
}

int 
LookupName(char *name,
                unsigned short port,
                struct sockaddr_storage *ret_addr,
                size_t *ret_addrlen) {
  struct addrinfo hints, *results;
  int retval;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  // Do the lookup by invoking getaddrinfo().
  if ((retval = getaddrinfo(name, NULL, &hints, &results)) != 0) {
    printf( "getaddrinfo failed: %s", gai_strerror(retval));
    return 0;
  }

  // Set the port in the first result.
  if (results->ai_family == AF_INET) {
    struct sockaddr_in *v4addr =
            (struct sockaddr_in *) (results->ai_addr);
    v4addr->sin_port = htons(port);
  } else if (results->ai_family == AF_INET6) {
    struct sockaddr_in6 *v6addr =
            (struct sockaddr_in6 *)(results->ai_addr);
    v6addr->sin6_port = htons(port);
  } else {
    printf("getaddrinfo failed to provide an IPv4 or IPv6 address \n");
    freeaddrinfo(results);
    return 0;
  }

  // Return the first result.
  assert(results != NULL);
  memcpy(ret_addr, results->ai_addr, results->ai_addrlen);
  *ret_addrlen = results->ai_addrlen;

  // Clean up.
  freeaddrinfo(results);
  return 1;
}

int 
Connect(const struct sockaddr_storage *addr,
             const size_t addrlen,
             int *ret_fd) {
  // Create the socket.
  int socket_fd = socket(addr->ss_family, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    printf("socket() failed: %s", strerror(errno));
    return 0;
  }

  // Connect the socket to the remote host.
  int res = connect(socket_fd,
                    (const struct sockaddr *)(addr),
                    addrlen);
  if (res == -1) {
    printf("connect() failed: %s", strerror(errno));
    return 0;
  }

  *ret_fd = socket_fd;
  return 1;
}
