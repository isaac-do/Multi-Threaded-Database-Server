#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include "common_threads.h"
#include "msg.h"

#define DATABASE_FILE "database"

void Usage(char *progname);
void PrintOut(int fd, struct sockaddr *addr, size_t addrlen);
void PrintReverseDNS(struct sockaddr *addr, size_t addrlen);
void PrintServerSide(int client_fd, int sock_family);

int  Listen(char *portnum, int *sock_family);
void* HandleClient(void* arg); // Each thread created will use this function to handle the client
void HandleClientRequests(int client_fd); // All client requests will go through this function

struct threadArg
{
	int c_fd; // client file descriptor
	struct sockaddr *addr; // socket address
	size_t addrlen; // address length
	int sock_family; // socket family
	int thread_id; // thread id for that thread
	int num_threads; // might not need this
};

int 
main(int argc, char **argv) {
  //Create a database file upon server startup
  FILE* fptr;
  fptr = fopen(DATABASE_FILE, "w"); // Create db file
  fclose(fptr);  // Close db file before each thread opens it

 
 // Expect the port number as a command line argument.
  if (argc != 2) {
    Usage(argv[0]);
  }

  int sock_family;
  int listen_fd = Listen(argv[1], &sock_family);
  if (listen_fd <= 0) {
    // We failed to bind/listen to a socket.  Quit with failure.
    printf("Couldn't bind to any addresses.\n");
    return EXIT_FAILURE;
  }
  int i; // Will iterate for every thread created
  // Loop forever, accepting a connection from a client and doing
  // an echo trick to it.
  while (1) {
    struct sockaddr_storage caddr;
    socklen_t caddr_len = sizeof(caddr);
    int client_fd = accept(listen_fd, (struct sockaddr *)&caddr, &caddr_len);
    pthread_t client_thread;
		   
    if (client_fd < 0) {
      if ((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK))
        continue;
      printf("Failure on accept:%s \n", strerror(errno));
      // need to add something that still closes all the other threads currently open if one connection fails
      break;
    }
    i++; // As long as connection was successful, increment i

    struct threadArg client_args; // Create a struct to store all the client args
    
    client_args.c_fd = client_fd;
    client_args.addr = (struct sockaddr *)&caddr;
    client_args.addrlen = caddr_len;
    client_args.sock_family = sock_family;
    client_args.thread_id = i;
    printf("creating thread: %i\n",client_args.thread_id); // TODO: REMOVE THIS LINE
    Pthread_create(&client_thread, NULL, HandleClient, (void*)&client_args);
    Pthread_join(client_thread, NULL);
  }

  // Close socket
  close(listen_fd);
  return EXIT_SUCCESS;
}

void Usage(char *progname) {
  printf("usage: %s port \n", progname);
  exit(EXIT_FAILURE);
}

void 
PrintOut(int fd, struct sockaddr *addr, size_t addrlen) {
  printf("Socket [%d] is bound to: \n", fd);
  if (addr->sa_family == AF_INET) {
    // Print out the IPV4 address and port

    char astring[INET_ADDRSTRLEN];
    struct sockaddr_in *in4 = (struct sockaddr_in *)(addr);
    inet_ntop(AF_INET, &(in4->sin_addr), astring, INET_ADDRSTRLEN);
    printf(" IPv4 address %s", astring);
    printf(" and port %d\n", ntohs(in4->sin_port));

  } else if (addr->sa_family == AF_INET6) {
    // Print out the IPV6 address and port

    char astring[INET6_ADDRSTRLEN];
    struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)(addr);
    inet_ntop(AF_INET6, &(in6->sin6_addr), astring, INET6_ADDRSTRLEN);
    printf("IPv6 address %s", astring);
    printf(" and port %d\n", ntohs(in6->sin6_port));

  } else {
    printf(" ???? address and port ???? \n");
  }
}

void 
PrintReverseDNS(struct sockaddr *addr, size_t addrlen) {
  char hostname[1024];  // ought to be big enough.
  if (getnameinfo(addr, addrlen, hostname, 1024, NULL, 0, 0) != 0) {
    sprintf(hostname, "[reverse DNS failed]");
  }
  printf("DNS name: %s \n", hostname);
}

void 
PrintServerSide(int client_fd, int sock_family) {
  char hname[1024];
  hname[0] = '\0';

  printf("Server side interface is ");
  if (sock_family == AF_INET) {
    // The server is using an IPv4 address.
    struct sockaddr_in srvr;
    socklen_t srvrlen = sizeof(srvr);
    char addrbuf[INET_ADDRSTRLEN];
    getsockname(client_fd, (struct sockaddr *) &srvr, &srvrlen);
    inet_ntop(AF_INET, &srvr.sin_addr, addrbuf, INET_ADDRSTRLEN);
    printf("%s", addrbuf);
    // Get the server's dns name, or return it's IP address as
    // a substitute if the dns lookup fails.
    getnameinfo((const struct sockaddr *) &srvr,
                srvrlen, hname, 1024, NULL, 0, 0);
    printf(" [%s]\n", hname);
  } else {
    // The server is using an IPv6 address.
    struct sockaddr_in6 srvr;
    socklen_t srvrlen = sizeof(srvr);
    char addrbuf[INET6_ADDRSTRLEN];
    getsockname(client_fd, (struct sockaddr *) &srvr, &srvrlen);
    inet_ntop(AF_INET6, &srvr.sin6_addr, addrbuf, INET6_ADDRSTRLEN);
    printf("%s", addrbuf);
    // Get the server's dns name, or return it's IP address as
    // a substitute if the dns lookup fails.
    getnameinfo((const struct sockaddr *) &srvr,
                srvrlen, hname, 1024, NULL, 0, 0);
    printf(" [%s]\n", hname);
  }
}

int 
Listen(char *portnum, int *sock_family) {

  // Populate the "hints" addrinfo structure for getaddrinfo().
  // ("man addrinfo")
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;       // IPv6 (also handles IPv4 clients)
  hints.ai_socktype = SOCK_STREAM;  // stream
  hints.ai_flags = AI_PASSIVE;      // use wildcard "in6addr_any" address
  hints.ai_flags |= AI_V4MAPPED;    // use v4-mapped v6 if no v6 found
  hints.ai_protocol = IPPROTO_TCP;  // tcp protocol
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  // Use argv[1] as the string representation of our portnumber to
  // pass in to getaddrinfo().  getaddrinfo() returns a list of
  // address structures via the output parameter "result".
  struct addrinfo *result;
  int res = getaddrinfo(NULL, portnum, &hints, &result);

  // Did addrinfo() fail?
  if (res != 0) {
	printf( "getaddrinfo failed: %s", gai_strerror(res));
    return -1;
  }

  // Loop through the returned address structures until we are able
  // to create a socket and bind to one.  The address structures are
  // linked in a list through the "ai_next" field of result.
  int listen_fd = -1;
  struct addrinfo *rp;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    listen_fd = socket(rp->ai_family,
                       rp->ai_socktype,
                       rp->ai_protocol);
    if (listen_fd == -1) {
      // Creating this socket failed.  So, loop to the next returned
      // result and try again.
      printf("socket() failed:%s \n ", strerror(errno));
      listen_fd = -1;
      continue;
    }

    // Configure the socket; we're setting a socket "option."  In
    // particular, we set "SO_REUSEADDR", which tells the TCP stack
    // so make the port we bind to available again as soon as we
    // exit, rather than waiting for a few tens of seconds to recycle it.
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));

    // Try binding the socket to the address and port number returned
    // by getaddrinfo().
    if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
      // Bind worked!  Print out the information about what
      // we bound to.
      PrintOut(listen_fd, rp->ai_addr, rp->ai_addrlen);

      // Return to the caller the address family.
      *sock_family = rp->ai_family;
      break;
    }

    // The bind failed.  Close the socket, then loop back around and
    // try the next address/port returned by getaddrinfo().
    close(listen_fd);
    listen_fd = -1;
  }

  // Free the structure returned by getaddrinfo().
  freeaddrinfo(result);

  // If we failed to bind, return failure.
  if (listen_fd == -1)
    return listen_fd;

  // Success. Tell the OS that we want this to be a listening socket.
  if (listen(listen_fd, SOMAXCONN) != 0) {
    printf("Failed to mark socket as listening:%s\n", strerror(errno));
    close(listen_fd);
    return -1;
  }

  // Return to the client the listening file descriptor.
  return listen_fd;
}

void* 
HandleClient(void* arg) {
  struct threadArg* args = (struct threadArg*)arg; // Get access to the thread properties

  // Print out information about the client.
  printf("\nNew client connection \n" );
  PrintOut((*args).c_fd, (*args).addr, (*args).addrlen);
  PrintReverseDNS((*args).addr, (*args).addrlen);
  PrintServerSide((*args).c_fd, (*args).sock_family);
  
  HandleClientRequests((*args).c_fd);

  printf("[The client disconnected.] \n");

  pthread_exit(NULL); // close thread
  close((*args).c_fd);
  return 0;
}

void HandleClientRequests(int client_fd) {
    struct msg clientRequest, clientResponse;
    FILE *fd = fopen(DATABASE_FILE, "r+");
    
    if (!fd) {
        perror("file open failed.");
        clientResponse.type = FAIL;
        write(client_fd, &clientResponse, sizeof(struct msg));
        return;
    }

    while(1) {
	    read(client_fd, &clientRequest, sizeof(struct msg));
	    
	    if(clientRequest.type == 0) // If user entered 0 break out of loop
	    	break;
	   
	    if (clientRequest.type == PUT) {
		if (fprintf(fd, "%u %s\n", clientRequest.rd.id, clientRequest.rd.name) < 0) {
		  perror("Failed to write to database.\n");
		  clientResponse.type = FAIL;
		} else {
		  fflush(fd);
		  clientResponse.type = SUCCESS;
		}
		write(client_fd, &clientResponse, sizeof(struct msg));
	    } else if (clientRequest.type == GET) {
		struct record dataBaseRecord;
		int found = 0;
		char buffer[MAX_NAME_LENGTH];
		rewind(fd);

                while (fgets(buffer, sizeof(buffer), fd) != NULL) {
		if (sscanf(buffer, "%u %[^\n]", &dataBaseRecord.id, dataBaseRecord.name) == 2) {
		//while (fscanf(fd, "%u %[^\n]", &dataBaseRecord.id, dataBaseRecord.name) == 2) {
		    if (dataBaseRecord.id == clientRequest.rd.id) {
			found = 1;
			//printf("name: %s\n id: %u\n", dataBaseRecord.name, dataBaseRecord.id);
			break;
		    }
		  }
		}

		if (found) {
		    clientResponse.type = SUCCESS;
		    clientResponse.rd = dataBaseRecord;
		} else {
		    clientResponse.type = FAIL;
		    perror("Get failed.\n");
		}
		write(client_fd, &clientResponse, sizeof(struct msg));
	    } else {
		clientResponse.type = FAIL;
		write(client_fd, &clientResponse, sizeof(struct msg));
		printf("Invalid request type received.\n");
	    }	    
    }
    fclose(fd);
}
