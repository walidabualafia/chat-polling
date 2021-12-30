/*
 * client.c - a turn taking chat client
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>

// constants for pipe FDs
#define WFD 1
#define RFD 0

/**
 * nonblock - a function that makes a file descriptor non-blocking
 * @param fd file descriptor
 */
void nonblock(int fd) {
  int flags;

  if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
    perror("fcntl (get):");
    exit(1);
  }
  if (fcntl(fd, F_SETFL, flags | FNDELAY) == -1) {
    perror("fcntl (set):");
    exit(1);
  }

}

/* get_in_addr - gets the sockaddr, regardless of sockaddr family
 * @param sa - sockaddr
 */
void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char **argv) {
  /* Variables */
  int sfd;                      // socket fd
  int opt;                      // flag options
  int num_events;               // number of events happening on an fd
  int rbytes, wbytes;           // read/write byte count
  char *defhost = "login02";    // default host for TCP/IP connection
  char *defport = "5055";       // default TCP port
  char buf[100];                // string buffer for exchanged texts
  char s[INET6_ADDRSTRLEN];     // used in finding IP addr
  struct pollfd pfds[2];        // array of poll fds
  // Socket Address
  struct sockaddr_in addr;
  struct addrinfo hints, *info, *p;

  /* Command Line Arguments can be set with 2 flags:
   *    - p [port #]
   *    - h [host] 
   */
  while ((opt = getopt(argc, argv, "h:p:")) != -1) {
    switch (opt) {
    case 'h':
      // set the host to be the argument specified by the user
      defhost = optarg;
    case 'p':
      // set the port to be the argument specified by the user
      defport = optarg;
    default:
      printf("usage: ./client [-h hostname] [-p port]\n");
    break;
    }
  }

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  
  // get the addr info into ADDRINFO info 
  if ((getaddrinfo(defhost, defport, &hints, &info)) == -1) {
    perror("getaddrinfo");
    exit(1);
  }

  // loop over all incoming connections and connect to first one found
  for (p = info; p != NULL; p = p->ai_next) {
    // initialize a TCP/IP socket
    if ((sfd = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == -1) {
      perror("client: socket");
      exit(1);
    }
    
    // attach client socket to server's socket
    if (connect(sfd, info->ai_addr, info->ai_addrlen) == -1) {
      perror("client: connect");
      close(sfd);
      exit(1);
    }

    break;
  }

  // if client did not connect, tell user
  if (p == NULL) {
    perror("client: failed to connect");
    exit(1);
  } else {
    // convert host IP address to string format
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("connected to server: %s ...\n", s);
  }
  
  // set the socket fd to be non-blocking
  nonblock(sfd);
  nonblock(STDIN_FILENO);

  // free the info var
  freeaddrinfo(info);
  
  do {
    pfds[0].fd = 0;     // STDIN
    pfds[0].events = POLLIN;
    pfds[1].fd = sfd;   // socket fd
    pfds[1].events = POLLIN;

    // call poll with no timeout
    if ((num_events = poll(pfds, 2, -100)) == -1) {
      perror("client: poll");
      exit(1);
    }
    
    // when poll returns, handle reads/writes
    if (num_events != 0) {
      int pollin_stdin = pfds[0].revents & POLLIN;
      int pollin_sfd = pfds[1].revents & POLLIN;

      if (pollin_stdin) {
        /* Read from STDIN */
        if ((rbytes = read(STDIN_FILENO, buf, sizeof buf)) == -1) {
          perror("client: read from stdin");
          exit(1);
        }
        if (rbytes == 0) {      // EOF
          break;
        }
        /* Write to SFD */
        if ((wbytes = write(sfd, buf, rbytes)) == -1) {
          perror("client: write to sfd");
          exit(1);
        }
        if (wbytes == 0) {      // EOF
          break;
        }
      } else if (pollin_sfd) {
        /* Read from SFD */
        if ((rbytes = read(sfd, buf, sizeof buf)) == -1) {
          perror("client: read from sfd");
          exit(1);
        }
        if (rbytes == 0) {      // EOF
          break;
        }
        /* Write to STDOUT */
        if ((wbytes = write(STDOUT_FILENO, buf, rbytes)) == -1) {
          perror("client: write to STDOUT");
          exit(1);
        }
        if (wbytes == 0) {      // EOF
          break;
        }
      }
    }
  } while (1);

  /* Cleanup Sequence */
  close(sfd);
  printf("hanging up\n");
  exit(0);
}
